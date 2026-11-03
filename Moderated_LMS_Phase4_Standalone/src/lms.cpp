#include "lms.hpp"
#include <random>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

namespace lms {
namespace {
void check(int rc, sqlite3* db, const std::string& context) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
        throw std::runtime_error(context + ": " + (db ? sqlite3_errmsg(db) : "SQLite error"));
}
class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        check(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr), db, "Prepare SQL");
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    sqlite3_stmt* get() { return stmt_; }
    void text(int i, const std::string& v) { check(sqlite3_bind_text(stmt_, i, v.c_str(), -1, SQLITE_TRANSIENT), db_, "Bind text"); }
    void integer(int i, int v) { check(sqlite3_bind_int(stmt_, i, v), db_, "Bind integer"); }
    void real(int i, double v) { check(sqlite3_bind_double(stmt_, i, v), db_, "Bind real"); }
    int step() { int rc=sqlite3_step(stmt_); check(rc, db_, "Execute SQL"); return rc; }
private:
    sqlite3* db_; sqlite3_stmt* stmt_{nullptr};
};
std::string colText(sqlite3_stmt* s, int i) {
    const unsigned char* p=sqlite3_column_text(s,i);
    return p ? reinterpret_cast<const char*>(p) : "";
}
std::shared_ptr<User> makeUser(int id, const std::string& username,
                               const std::string& fullName, Role role, bool active) {
    std::shared_ptr<User> u;
    switch(role) {
        case Role::Student: u=std::make_shared<Student>(); break;
        case Role::Lecturer: u=std::make_shared<Lecturer>(); break;
        case Role::HoD: u=std::make_shared<HeadOfDepartment>(); break;
        case Role::Administrator: u=std::make_shared<Administrator>(); break;
    }
    u->id=id; u->username=username; u->fullName=fullName; u->role=role; u->active=active;
    return u;
}
int daysLate(const std::string& submitted, const std::string& deadline) {
    // Validate both timestamps before using lexicographic ISO-8601 ordering.
    std::tm a{}, b{};
    std::istringstream sa(submitted), sb(deadline);
    sa >> std::get_time(&a, "%Y-%m-%d %H:%M");
    sb >> std::get_time(&b, "%Y-%m-%d %H:%M");
    sa >> std::ws; sb >> std::ws;
    if ((sa.fail() && !sa.eof()) || (sb.fail() && !sb.eof()) ||
        sa.rdbuf()->in_avail() > 0 || sb.rdbuf()->in_avail() > 0)
        throw std::invalid_argument("Date/time must use YYYY-MM-DD HH:MM.");
    const std::tm originalA = a, originalB = b;
    auto ta=std::mktime(&a), tb=std::mktime(&b);
    if (ta < 0 || tb < 0) throw std::invalid_argument("Invalid date/time.");
    std::tm normalizedA{}, normalizedB{};
#ifdef _WIN32
    localtime_s(&normalizedA, &ta); localtime_s(&normalizedB, &tb);
#else
    localtime_r(&ta, &normalizedA); localtime_r(&tb, &normalizedB);
#endif
    auto sameMinute = [](const std::tm& x, const std::tm& y) {
        return x.tm_year == y.tm_year && x.tm_mon == y.tm_mon && x.tm_mday == y.tm_mday &&
               x.tm_hour == y.tm_hour && x.tm_min == y.tm_min;
    };
    if (!sameMinute(originalA, normalizedA) || !sameMinute(originalB, normalizedB))
        throw std::invalid_argument("Invalid calendar date or time.");
    if (ta <= tb) return 0;
    auto seconds=std::difftime(ta,tb);
    return static_cast<int>(std::ceil(seconds/86400.0));
}
void requireRole(const User& u, Role r, const std::string& action) {
    if (!u.active || u.role != r) throw std::runtime_error("Access denied: role not permitted to " + action + ".");
}
}

std::string roleToString(Role r) {
    switch(r) { case Role::Student:return "STUDENT"; case Role::Lecturer:return "LECTURER";
        case Role::HoD:return "HOD"; case Role::Administrator:return "ADMIN"; }
    return "STUDENT";
}
Role roleFromString(const std::string& v) {
    if(v=="STUDENT") return Role::Student;
    if(v=="LECTURER") return Role::Lecturer;
    if(v=="HOD") return Role::HoD;
    if(v=="ADMIN") return Role::Administrator;
    throw std::invalid_argument("Unknown role.");
}
Database::Database(const std::string& path) {
    std::filesystem::path p(path);
    if(p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    if(sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string e=db_?sqlite3_errmsg(db_):"Could not open database";
        if(db_) sqlite3_close(db_);
        db_=nullptr; throw std::runtime_error(e);
    }
    sqlite3_busy_timeout(db_, 5000);
    execute("PRAGMA foreign_keys = ON;");
    execute("PRAGMA journal_mode = WAL;");
    execute(R"SQL(
CREATE TABLE IF NOT EXISTS Users(
 user_id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE, full_name TEXT NOT NULL,
 password_hash TEXT NOT NULL, role TEXT NOT NULL CHECK(role IN('STUDENT','LECTURER','HOD','ADMIN')),
 active INTEGER NOT NULL DEFAULT 1 CHECK(active IN(0,1)));
CREATE TABLE IF NOT EXISTS Departments(
 department_id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, hod_user_id INTEGER,
 FOREIGN KEY(hod_user_id) REFERENCES Users(user_id));
CREATE TABLE IF NOT EXISTS Courses(
 course_id INTEGER PRIMARY KEY, department_id INTEGER NOT NULL, course_code TEXT NOT NULL UNIQUE,
 course_name TEXT NOT NULL, lecturer_user_id INTEGER,
 FOREIGN KEY(department_id) REFERENCES Departments(department_id),
 FOREIGN KEY(lecturer_user_id) REFERENCES Users(user_id));
CREATE TABLE IF NOT EXISTS Enrolments(
 enrolment_id INTEGER PRIMARY KEY, student_user_id INTEGER NOT NULL, course_id INTEGER NOT NULL,
 status TEXT NOT NULL DEFAULT 'ACTIVE' CHECK(status IN('ACTIVE','INACTIVE')),
 UNIQUE(student_user_id,course_id), FOREIGN KEY(student_user_id) REFERENCES Users(user_id),
 FOREIGN KEY(course_id) REFERENCES Courses(course_id));
CREATE TABLE IF NOT EXISTS Assignments(
 assignment_id INTEGER PRIMARY KEY, course_id INTEGER NOT NULL, title TEXT NOT NULL,
 instructions TEXT, deadline TEXT NOT NULL, max_mark REAL NOT NULL CHECK(max_mark>0),
 FOREIGN KEY(course_id) REFERENCES Courses(course_id));
CREATE TABLE IF NOT EXISTS Extensions(
 extension_id INTEGER PRIMARY KEY, assignment_id INTEGER NOT NULL, student_user_id INTEGER NOT NULL,
 granted_by INTEGER NOT NULL, new_deadline TEXT NOT NULL, reason TEXT NOT NULL, granted_at TEXT NOT NULL,
 FOREIGN KEY(assignment_id) REFERENCES Assignments(assignment_id),
 FOREIGN KEY(student_user_id) REFERENCES Users(user_id), FOREIGN KEY(granted_by) REFERENCES Users(user_id));
CREATE TABLE IF NOT EXISTS Submissions(
 submission_id INTEGER PRIMARY KEY, assignment_id INTEGER NOT NULL, student_user_id INTEGER NOT NULL,
 submitted_at TEXT NOT NULL, status TEXT NOT NULL CHECK(status IN('ON_TIME','LATE','BEYOND_ALLOWED_PERIOD')),
 version_no INTEGER NOT NULL CHECK(version_no>0), file_path TEXT NOT NULL,
 is_latest INTEGER NOT NULL DEFAULT 1 CHECK(is_latest IN(0,1)),
 UNIQUE(assignment_id,student_user_id,version_no),
 FOREIGN KEY(assignment_id) REFERENCES Assignments(assignment_id),
 FOREIGN KEY(student_user_id) REFERENCES Users(user_id));
CREATE UNIQUE INDEX IF NOT EXISTS one_latest_submission
 ON Submissions(assignment_id,student_user_id) WHERE is_latest=1;
CREATE TABLE IF NOT EXISTS Marks(
 mark_id INTEGER PRIMARY KEY, submission_id INTEGER NOT NULL UNIQUE, lecturer_user_id INTEGER NOT NULL,
 raw_mark REAL NOT NULL CHECK(raw_mark>=0), final_mark REAL NOT NULL CHECK(final_mark>=0),
 feedback TEXT, moderation_status TEXT NOT NULL DEFAULT 'PENDING'
 CHECK(moderation_status IN('PENDING','RETURNED','APPROVED','RELEASED')), released_at TEXT,
 FOREIGN KEY(submission_id) REFERENCES Submissions(submission_id),
 FOREIGN KEY(lecturer_user_id) REFERENCES Users(user_id));
CREATE TABLE IF NOT EXISTS Mark_Audit(
 audit_id INTEGER PRIMARY KEY, mark_id INTEGER NOT NULL, changed_by INTEGER NOT NULL,
 changed_at TEXT NOT NULL, old_mark REAL, new_mark REAL, reason TEXT NOT NULL, action_type TEXT NOT NULL,
 FOREIGN KEY(mark_id) REFERENCES Marks(mark_id), FOREIGN KEY(changed_by) REFERENCES Users(user_id));
)SQL");
}
Database::~Database(){ if(db_) sqlite3_close(db_); }
void Database::execute(const std::string& sql) {
    char* err=nullptr; int rc=sqlite3_exec(db_,sql.c_str(),nullptr,nullptr,&err);
    if(rc!=SQLITE_OK){std::string e=err?err:"SQL error";sqlite3_free(err);throw std::runtime_error(e);}
}
std::string PasswordHasher::hashPassword(const std::string& password) {
    if (password.size() < 8) throw std::invalid_argument("Password must contain at least 8 characters.");
    constexpr int iterations = 200000;
    std::vector<unsigned char> salt(16), hash(32);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1)
        throw std::runtime_error("OpenSSL could not generate a cryptographically secure salt.");
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          salt.data(), static_cast<int>(salt.size()), iterations,
                          EVP_sha256(), static_cast<int>(hash.size()), hash.data()) != 1)
        throw std::runtime_error("OpenSSL PBKDF2 password hashing failed.");
    std::ostringstream out;
    out << "pbkdf2_sha256$" << iterations << "$" << std::hex << std::setfill('0');
    for (unsigned char c : salt) out << std::setw(2) << static_cast<int>(c);
    out << "$";
    for (unsigned char c : hash) out << std::setw(2) << static_cast<int>(c);
    return out.str();
}
bool PasswordHasher::verifyPassword(const std::string& password, const std::string& stored) {
    try {
        std::vector<std::string> parts;
        std::stringstream stream(stored);
        std::string part;
        while (std::getline(stream, part, '$')) parts.push_back(part);
        if (parts.size() != 4 || parts[0] != "pbkdf2_sha256" || parts[1] != "200000") return false;
        auto decode = [](const std::string& value) {
            if (value.size() % 2) throw std::runtime_error("Malformed password hash.");
            std::vector<unsigned char> bytes;
            for (size_t i = 0; i < value.size(); i += 2) {
                size_t used = 0;
                unsigned long v = std::stoul(value.substr(i, 2), &used, 16);
                if (used != 2 || v > 255) throw std::runtime_error("Malformed password hash.");
                bytes.push_back(static_cast<unsigned char>(v));
            }
            return bytes;
        };
        const auto salt = decode(parts[2]);
        const auto expected = decode(parts[3]);
        if (salt.size() != 16 || expected.size() != 32) return false;
        std::vector<unsigned char> actual(32);
        if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                              salt.data(), static_cast<int>(salt.size()), 200000,
                              EVP_sha256(), static_cast<int>(actual.size()), actual.data()) != 1)
            return false;
        return CRYPTO_memcmp(actual.data(), expected.data(), actual.size()) == 0;
    } catch (...) { return false; }
}

double LatePenaltyStrategy::calculate(double raw,double max,const std::string& submitted,
 const std::string& deadline,bool extension) const {
    if(extension || submitted<=deadline) return raw;
    int days=daysLate(submitted,deadline);
    if(days>3) return 0.0;
    return std::max(0.0,raw-(0.10*max*days));
}

int UserRepository::create(const std::string& username,const std::string& name,
 const std::string& password,Role role) {
    if(username.empty()||name.empty()) throw std::invalid_argument("Username and full name are required.");
    Statement s(db_.handle(),"INSERT INTO Users(username,full_name,password_hash,role) VALUES(?,?,?,?)");
    s.text(1,username);s.text(2,name);s.text(3,PasswordHasher::hashPassword(password));s.text(4,roleToString(role));s.step();
    return static_cast<int>(sqlite3_last_insert_rowid(db_.handle()));
}
std::shared_ptr<User> UserRepository::authenticate(const std::string& username,const std::string& password) {
    Statement s(db_.handle(),"SELECT user_id,username,full_name,password_hash,role,active FROM Users WHERE username=?");
    s.text(1,username); if(s.step()!=SQLITE_ROW) return {};
    if(sqlite3_column_int(s.get(),5)!=1 || !PasswordHasher::verifyPassword(password,colText(s.get(),3))) return {};
    return makeUser(sqlite3_column_int(s.get(),0),colText(s.get(),1),colText(s.get(),2),
                    roleFromString(colText(s.get(),4)),true);
}
std::shared_ptr<User> UserRepository::findById(int id) {
    Statement s(db_.handle(),"SELECT user_id,username,full_name,role,active FROM Users WHERE user_id=?");
    s.integer(1,id); if(s.step()!=SQLITE_ROW)return {};
    return makeUser(sqlite3_column_int(s.get(),0),colText(s.get(),1),colText(s.get(),2),
       roleFromString(colText(s.get(),3)),sqlite3_column_int(s.get(),4)==1);
}
std::vector<std::shared_ptr<User>> UserRepository::all() {
    Statement s(db_.handle(),"SELECT user_id,username,full_name,role,active FROM Users ORDER BY user_id");
    std::vector<std::shared_ptr<User>> out; while(s.step()==SQLITE_ROW)
      out.push_back(makeUser(sqlite3_column_int(s.get(),0),colText(s.get(),1),colText(s.get(),2),
        roleFromString(colText(s.get(),3)),sqlite3_column_int(s.get(),4)==1));
    return out;
}
void UserRepository::setActive(int id,bool active) {
    Statement s(db_.handle(),"UPDATE Users SET active=? WHERE user_id=?");s.integer(1,active?1:0);s.integer(2,id);s.step();
}
int AcademicRepository::createDepartment(const std::string& name,int hod) {
    Statement s(db_.handle(),"INSERT INTO Departments(name,hod_user_id) VALUES(?,?)");
    s.text(1,name);s.integer(2,hod);s.step();return static_cast<int>(sqlite3_last_insert_rowid(db_.handle()));
}
int AcademicRepository::createCourse(int dep,const std::string& code,const std::string& name,int lecturer) {
    Statement s(db_.handle(),"INSERT INTO Courses(department_id,course_code,course_name,lecturer_user_id) VALUES(?,?,?,?)");
    s.integer(1,dep);s.text(2,code);s.text(3,name);s.integer(4,lecturer);s.step();return static_cast<int>(sqlite3_last_insert_rowid(db_.handle()));
}
void AcademicRepository::enrol(int student,int course) {
    Statement s(db_.handle(),"INSERT INTO Enrolments(student_user_id,course_id) VALUES(?,?)");
    s.integer(1,student);s.integer(2,course);s.step();
}
std::vector<std::string> AcademicRepository::coursesForStudent(int id) {
    Statement s(db_.handle(),"SELECT c.course_id||' | '||c.course_code||' | '||c.course_name FROM Courses c JOIN Enrolments e ON e.course_id=c.course_id WHERE e.student_user_id=? AND e.status='ACTIVE'");
    s.integer(1,id);std::vector<std::string> v;while(s.step()==SQLITE_ROW)v.push_back(colText(s.get(),0));return v;
}
std::vector<std::string> AcademicRepository::coursesForLecturer(int id) {
    Statement s(db_.handle(),"SELECT course_id||' | '||course_code||' | '||course_name FROM Courses WHERE lecturer_user_id=?");
    s.integer(1,id);std::vector<std::string> v;while(s.step()==SQLITE_ROW)v.push_back(colText(s.get(),0));return v;
}
bool AcademicRepository::studentEnrolled(int st,int c) {
    Statement s(db_.handle(),"SELECT 1 FROM Enrolments WHERE student_user_id=? AND course_id=? AND status='ACTIVE'");
    s.integer(1,st);s.integer(2,c);return s.step()==SQLITE_ROW;
}
bool AcademicRepository::lecturerAssigned(int l,int c) {
    Statement s(db_.handle(),"SELECT 1 FROM Courses WHERE course_id=? AND lecturer_user_id=?");s.integer(1,c);s.integer(2,l);return s.step()==SQLITE_ROW;
}
bool AcademicRepository::hodOwnsCourse(int h,int c) {
    Statement s(db_.handle(),"SELECT 1 FROM Courses c JOIN Departments d ON d.department_id=c.department_id WHERE c.course_id=? AND d.hod_user_id=?");
    s.integer(1,c);s.integer(2,h);return s.step()==SQLITE_ROW;
}
std::vector<std::string> AcademicRepository::departments() {
    Statement s(db_.handle(),"SELECT department_id||' | '||name FROM Departments ORDER BY name");
    std::vector<std::string> v;while(s.step()==SQLITE_ROW)v.push_back(colText(s.get(),0));return v;
}
int AssignmentRepository::create(int course,const std::string& title,const std::string& instr,const std::string& deadline,double max) {
    if(title.empty()||max<=0)throw std::invalid_argument("Title and a positive maximum mark are required.");
    Statement s(db_.handle(),"INSERT INTO Assignments(course_id,title,instructions,deadline,max_mark) VALUES(?,?,?,?,?)");
    s.integer(1,course);s.text(2,title);s.text(3,instr);s.text(4,deadline);s.real(5,max);s.step();
    return static_cast<int>(sqlite3_last_insert_rowid(db_.handle()));
}
Assignment AssignmentRepository::get(int id) {
    Statement s(db_.handle(),"SELECT assignment_id,course_id,title,instructions,deadline,max_mark FROM Assignments WHERE assignment_id=?");
    s.integer(1,id);if(s.step()!=SQLITE_ROW)throw std::runtime_error("Assignment not found.");
    return {sqlite3_column_int(s.get(),0),sqlite3_column_int(s.get(),1),colText(s.get(),2),colText(s.get(),3),colText(s.get(),4),sqlite3_column_double(s.get(),5)};
}
std::vector<Assignment> AssignmentRepository::forCourse(int c) {
    Statement s(db_.handle(),"SELECT assignment_id,course_id,title,instructions,deadline,max_mark FROM Assignments WHERE course_id=?");
    s.integer(1,c);std::vector<Assignment> v;while(s.step()==SQLITE_ROW)v.push_back({sqlite3_column_int(s.get(),0),sqlite3_column_int(s.get(),1),colText(s.get(),2),colText(s.get(),3),colText(s.get(),4),sqlite3_column_double(s.get(),5)});return v;
}
std::vector<Assignment> AssignmentRepository::forStudent(int st) {
    Statement s(db_.handle(),"SELECT a.assignment_id,a.course_id,a.title,a.instructions,a.deadline,a.max_mark FROM Assignments a JOIN Enrolments e ON e.course_id=a.course_id WHERE e.student_user_id=? AND e.status='ACTIVE'");
    s.integer(1,st);std::vector<Assignment> v;while(s.step()==SQLITE_ROW)v.push_back({sqlite3_column_int(s.get(),0),sqlite3_column_int(s.get(),1),colText(s.get(),2),colText(s.get(),3),colText(s.get(),4),sqlite3_column_double(s.get(),5)});return v;
}
void AssignmentRepository::grantExtension(int a,int st,int lecturer,const std::string& deadline,const std::string& reason) {
    if(reason.empty())throw std::invalid_argument("An extension reason is required.");
    Statement s(db_.handle(),"INSERT INTO Extensions(assignment_id,student_user_id,granted_by,new_deadline,reason,granted_at) VALUES(?,?,?,?,?,?)");
    s.integer(1,a);s.integer(2,st);s.integer(3,lecturer);s.text(4,deadline);s.text(5,reason);s.text(6,nowLocal());s.step();
}
std::string AssignmentRepository::applicableDeadline(int a,int st) {
    Statement s(db_.handle(),"SELECT new_deadline FROM Extensions WHERE assignment_id=? AND student_user_id=? ORDER BY new_deadline DESC LIMIT 1");
    s.integer(1,a);s.integer(2,st);if(s.step()==SQLITE_ROW)return colText(s.get(),0);
    return get(a).deadline;
}
int SubmissionRepository::save(int a,int st,const std::string& at,const std::string& status,const std::string& path) {
    Statement q(db_.handle(),"SELECT COALESCE(MAX(version_no),0)+1 FROM Submissions WHERE assignment_id=? AND student_user_id=?");
    q.integer(1,a);q.integer(2,st);q.step();int version=sqlite3_column_int(q.get(),0);
    Statement u(db_.handle(),"UPDATE Submissions SET is_latest=0 WHERE assignment_id=? AND student_user_id=?");
    u.integer(1,a);u.integer(2,st);u.step();
    Statement s(db_.handle(),"INSERT INTO Submissions(assignment_id,student_user_id,submitted_at,status,version_no,file_path,is_latest) VALUES(?,?,?,?,?,?,1)");
    s.integer(1,a);s.integer(2,st);s.text(3,at);s.text(4,status);s.integer(5,version);s.text(6,path);s.step();
    return static_cast<int>(sqlite3_last_insert_rowid(db_.handle()));
}
std::vector<Submission> SubmissionRepository::pendingForLecturer(int l) {
    Statement s(db_.handle(),R"SQL(SELECT s.submission_id,s.assignment_id,s.student_user_id,s.submitted_at,s.status,s.version_no,s.file_path,s.is_latest
 FROM Submissions s JOIN Assignments a ON a.assignment_id=s.assignment_id JOIN Courses c ON c.course_id=a.course_id
 LEFT JOIN Marks m ON m.submission_id=s.submission_id
 WHERE c.lecturer_user_id=? AND s.is_latest=1 AND m.mark_id IS NULL ORDER BY s.submitted_at)SQL");
    s.integer(1,l);std::vector<Submission> v;while(s.step()==SQLITE_ROW)v.push_back({sqlite3_column_int(s.get(),0),sqlite3_column_int(s.get(),1),sqlite3_column_int(s.get(),2),colText(s.get(),3),colText(s.get(),4),sqlite3_column_int(s.get(),5),colText(s.get(),6),sqlite3_column_int(s.get(),7)==1});return v;
}
std::vector<Submission> SubmissionRepository::latestForAssignment(int a) {
    Statement s(db_.handle(),"SELECT submission_id,assignment_id,student_user_id,submitted_at,status,version_no,file_path,is_latest FROM Submissions WHERE assignment_id=? AND is_latest=1");
    s.integer(1,a);std::vector<Submission> v;while(s.step()==SQLITE_ROW)v.push_back({sqlite3_column_int(s.get(),0),sqlite3_column_int(s.get(),1),sqlite3_column_int(s.get(),2),colText(s.get(),3),colText(s.get(),4),sqlite3_column_int(s.get(),5),colText(s.get(),6),true});return v;
}
Submission SubmissionRepository::get(int id) {
    Statement s(db_.handle(),"SELECT submission_id,assignment_id,student_user_id,submitted_at,status,version_no,file_path,is_latest FROM Submissions WHERE submission_id=?");
    s.integer(1,id);if(s.step()!=SQLITE_ROW)throw std::runtime_error("Submission not found.");
    return {sqlite3_column_int(s.get(),0),sqlite3_column_int(s.get(),1),sqlite3_column_int(s.get(),2),colText(s.get(),3),colText(s.get(),4),sqlite3_column_int(s.get(),5),colText(s.get(),6),sqlite3_column_int(s.get(),7)==1};
}
int MarkRepository::saveOrUpdate(int sub,int lecturer,double raw,double finalMark,const std::string& feedback,int by,const std::string& reason) {
    Statement old(db_.handle(),"SELECT mark_id,final_mark FROM Marks WHERE submission_id=?");old.integer(1,sub);
    int id=0; double oldMark=-1;
    if(old.step()==SQLITE_ROW){id=sqlite3_column_int(old.get(),0);oldMark=sqlite3_column_double(old.get(),1);}
    if(id==0) {
        Statement s(db_.handle(),"INSERT INTO Marks(submission_id,lecturer_user_id,raw_mark,final_mark,feedback,moderation_status) VALUES(?,?,?,?,?,'PENDING')");
        s.integer(1,sub);s.integer(2,lecturer);s.real(3,raw);s.real(4,finalMark);s.text(5,feedback);s.step();id=static_cast<int>(sqlite3_last_insert_rowid(db_.handle()));
    } else {
        Statement s(db_.handle(),"UPDATE Marks SET lecturer_user_id=?,raw_mark=?,final_mark=?,feedback=?,moderation_status='PENDING',released_at=NULL WHERE mark_id=?");
        s.integer(1,lecturer);s.real(2,raw);s.real(3,finalMark);s.text(4,feedback);s.integer(5,id);s.step();
    }
    Statement a(db_.handle(),"INSERT INTO Mark_Audit(mark_id,changed_by,changed_at,old_mark,new_mark,reason,action_type) VALUES(?,?,?,?,?,?,?)");
    a.integer(1,id);a.integer(2,by);a.text(3,nowLocal());if(oldMark<0)sqlite3_bind_null(a.get(),4);else a.real(4,oldMark);
    a.real(5,finalMark);a.text(6,reason.empty()?"Initial marking":reason);a.text(7,oldMark<0?"MARKED":"MARK_CHANGED");a.step();
    return id;
}
std::vector<std::string> MarkRepository::pendingForHoD(int hod) {
    Statement s(db_.handle(),R"SQL(SELECT m.mark_id||' | Submission '||m.submission_id||' | '||m.final_mark||' | '||m.moderation_status
 FROM Marks m JOIN Submissions s ON s.submission_id=m.submission_id JOIN Assignments a ON a.assignment_id=s.assignment_id
 JOIN Courses c ON c.course_id=a.course_id JOIN Departments d ON d.department_id=c.department_id
 WHERE d.hod_user_id=? AND m.moderation_status IN('PENDING','RETURNED','APPROVED') ORDER BY m.mark_id)SQL");
    s.integer(1,hod);std::vector<std::string> v;while(s.step()==SQLITE_ROW)v.push_back(colText(s.get(),0));return v;
}
void MarkRepository::moderate(int id,int hod,bool approve,const std::string& reason) {
    if(reason.empty())throw std::invalid_argument("A moderation reason is required.");
    Statement checkq(db_.handle(),R"SQL(SELECT 1 FROM Marks m JOIN Submissions s ON s.submission_id=m.submission_id
 JOIN Assignments a ON a.assignment_id=s.assignment_id JOIN Courses c ON c.course_id=a.course_id
 JOIN Departments d ON d.department_id=c.department_id WHERE m.mark_id=? AND d.hod_user_id=?)SQL");
    checkq.integer(1,id);checkq.integer(2,hod);if(checkq.step()!=SQLITE_ROW)throw std::runtime_error("HoD is not responsible for this mark.");
    Statement s(db_.handle(),"UPDATE Marks SET moderation_status=? WHERE mark_id=?");
    s.text(1,approve?"APPROVED":"RETURNED");s.integer(2,id);s.step();
    Statement a(db_.handle(),"INSERT INTO Mark_Audit(mark_id,changed_by,changed_at,old_mark,new_mark,reason,action_type) VALUES(?,?,?,NULL,NULL,?,?)");
    a.integer(1,id);a.integer(2,hod);a.text(3,nowLocal());a.text(4,reason);a.text(5,approve?"MODERATION_APPROVED":"RETURNED_FOR_CORRECTION");a.step();
}
void MarkRepository::release(int id,int hod) {
    Statement q(db_.handle(),R"SQL(SELECT 1 FROM Marks m JOIN Submissions s ON s.submission_id=m.submission_id
 JOIN Assignments a ON a.assignment_id=s.assignment_id JOIN Courses c ON c.course_id=a.course_id
 JOIN Departments d ON d.department_id=c.department_id WHERE m.mark_id=? AND d.hod_user_id=? AND m.moderation_status='APPROVED')SQL");
    q.integer(1,id);q.integer(2,hod);if(q.step()!=SQLITE_ROW)throw std::runtime_error("Only an approved mark in your department can be released.");
    Statement s(db_.handle(),"UPDATE Marks SET moderation_status='RELEASED',released_at=? WHERE mark_id=?");
    s.text(1,nowLocal());s.integer(2,id);s.step();
    Statement a(db_.handle(),"INSERT INTO Mark_Audit(mark_id,changed_by,changed_at,old_mark,new_mark,reason,action_type) VALUES(?,?,?,NULL,NULL,'HoD released approved mark','RELEASED')");
    a.integer(1,id);a.integer(2,hod);a.text(3,nowLocal());a.step();
}
std::vector<std::string> MarkRepository::releasedForStudent(int st) {
    Statement s(db_.handle(),R"SQL(SELECT a.title||' | Mark: '||m.final_mark||'/'||a.max_mark||' | Feedback: '||COALESCE(m.feedback,'')
 FROM Marks m JOIN Submissions sub ON sub.submission_id=m.submission_id
 JOIN Assignments a ON a.assignment_id=sub.assignment_id
 WHERE sub.student_user_id=? AND sub.is_latest=1 AND m.moderation_status='RELEASED')SQL");
    s.integer(1,st);std::vector<std::string> v;while(s.step()==SQLITE_ROW)v.push_back(colText(s.get(),0));return v;
}

LMSService::LMSService(const std::string& path)
 : db_(path), penalty_(std::make_unique<LatePenaltyStrategy>()), users(db_),
   academics(db_), assignments(db_), submissions(db_), marks(db_) {}
int LMSService::createUser(const User& actor,const std::string& username,const std::string& name,const std::string& pw,Role role) {
    if(!actor.active || !actor.canManageUsers())throw std::runtime_error("Only an active administrator can create users.");
    return users.create(username,name,pw,role);
}
int LMSService::createDepartment(const User& actor,const std::string& name,int hodId) {
    if (!actor.active || !actor.canManageCourses()) throw std::runtime_error("Only an active administrator can create departments.");
    auto hod = users.findById(hodId);
    if (!hod || !hod->active || hod->role != Role::HoD) throw std::invalid_argument("Department head must be an active HoD account.");
    return academics.createDepartment(name, hodId);
}
int LMSService::createCourse(const User& actor,int departmentId,const std::string& code,const std::string& name,int lecturerId) {
    if (!actor.active || !actor.canManageCourses()) throw std::runtime_error("Only an active administrator can create courses.");
    auto lecturer = users.findById(lecturerId);
    if (!lecturer || !lecturer->active || lecturer->role != Role::Lecturer) throw std::invalid_argument("Course lecturer must be an active lecturer account.");
    return academics.createCourse(departmentId, code, name, lecturerId);
}
void LMSService::enrolStudent(const User& actor,int studentId,int courseId) {
    if (!actor.active || !actor.canManageCourses()) throw std::runtime_error("Only an active administrator can enrol students.");
    auto student = users.findById(studentId);
    if (!student || !student->active || student->role != Role::Student) throw std::invalid_argument("Enrolment target must be an active student account.");
    academics.enrol(studentId, courseId);
}
void LMSService::setUserActive(const User& actor,int userId,bool active) {
    if (!actor.active || actor.role != Role::Administrator) throw std::runtime_error("Only an active administrator can change account status.");
    if (!users.findById(userId)) throw std::invalid_argument("User account not found.");
    users.setActive(userId, active);
}
void LMSService::grantExtension(const User& actor,int assignmentId,int studentId,const std::string& deadline,const std::string& reason) {
    if (!actor.active || !actor.canTeach()) throw std::runtime_error("Only an active lecturer can grant extensions.");
    const Assignment assignment = assignments.get(assignmentId);
    if (!academics.lecturerAssigned(actor.id, assignment.courseId)) throw std::runtime_error("Only the assigned lecturer can grant this extension.");
    if (!academics.studentEnrolled(studentId, assignment.courseId)) throw std::invalid_argument("Student is not enrolled in this assignment's course.");
    (void)daysLate(deadline, deadline);
    assignments.grantExtension(assignmentId, studentId, actor.id, deadline, reason);
}
int LMSService::createAssignment(const User& actor,int course,const std::string& title,const std::string& instr,const std::string& deadline,double max) {
    if(!actor.active || !actor.canTeach()||!academics.lecturerAssigned(actor.id,course))throw std::runtime_error("Only the assigned lecturer can create this assignment.");
    (void)daysLate(deadline, deadline);
    return assignments.create(course,title,instr,deadline,max);
}
int LMSService::submit(const User& actor,int aid,const std::string& at,const std::string& source) {
    requireRole(actor,Role::Student,"submit work");
    Assignment a=assignments.get(aid);
    if(!academics.studentEnrolled(actor.id,a.courseId))throw std::runtime_error("You are not enrolled in this course.");
    if(source.empty()||!std::filesystem::exists(source)||!std::filesystem::is_regular_file(source))
        throw std::invalid_argument("The submitted file does not exist or is not a regular file.");
    std::string deadline=assignments.applicableDeadline(aid,actor.id);
    int late=daysLate(at,deadline);
    if(late>3) throw std::runtime_error("Submission rejected: more than three days after the applicable deadline.");
    std::filesystem::create_directories("storage/submissions");
    int next=1;
    for(const auto& s:submissions.latestForAssignment(aid)) if(s.studentId==actor.id) next=s.version+1;
    std::string dest="storage/submissions/a"+std::to_string(aid)+"_u"+std::to_string(actor.id)+"_v"+std::to_string(next)+std::filesystem::path(source).extension().string();
    std::filesystem::copy_file(source,dest,std::filesystem::copy_options::overwrite_existing);
    std::string status=late==0?"ON_TIME":"LATE";
    return submissions.save(aid,actor.id,at,status,dest);
}
void LMSService::markSubmission(const User& actor,int sid,double raw,const std::string& feedback,const std::string& reason) {
    if(!actor.active || !actor.canTeach())throw std::runtime_error("Only active lecturers can mark submissions.");
    Submission s=submissions.get(sid);Assignment a=assignments.get(s.assignmentId);
    if(!academics.lecturerAssigned(actor.id,a.courseId))throw std::runtime_error("You are not assigned to this course.");
    if(raw<0||raw>a.maxMark)throw std::invalid_argument("Mark must be between 0 and the assignment maximum.");
    std::string deadline=assignments.applicableDeadline(a.id,s.studentId);
    double finalMark=penalty_->calculate(raw,a.maxMark,s.submittedAt,deadline,false);
    marks.saveOrUpdate(sid,actor.id,raw,finalMark,feedback,actor.id,reason);
}
void LMSService::moderate(const User& actor,int id,bool approve,const std::string& reason) {
    requireRole(actor,Role::HoD,"moderate marks");marks.moderate(id,actor.id,approve,reason);
}
void LMSService::release(const User& actor,int id) {
    requireRole(actor,Role::HoD,"release marks");marks.release(id,actor.id);
}

std::string nowLocal() {
    auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm,&t);
#else
    localtime_r(&t,&tm);
#endif
    std::ostringstream o;o<<std::put_time(&tm,"%Y-%m-%d %H:%M");return o.str();
}
std::string prompt(const std::string& label) { std::cout<<label;std::string v;std::getline(std::cin,v);return v; }
int promptInt(const std::string& label) { auto s=prompt(label);try{size_t n;int v=std::stoi(s,&n);if(n!=s.size())throw std::invalid_argument("x");return v;}catch(...){throw std::invalid_argument("Please enter a whole number.");} }
double promptDouble(const std::string& label) { auto s=prompt(label);try{size_t n;double v=std::stod(s,&n);if(n!=s.size()||!std::isfinite(v))throw std::invalid_argument("x");return v;}catch(...){throw std::invalid_argument("Please enter a valid number.");} }

void runApplication() {
    LMSService app("data/lms.db");
    // Bootstrap first administrator only when the database has no users.
    if(app.users.all().empty()) {
        std::cout<<"First run: create the initial administrator.\n";
        auto un=prompt("Admin username: "), name=prompt("Full name: "), pw=prompt("Password (8+ characters): ");
        app.users.create(un,name,pw,Role::Administrator);
        std::cout<<"Administrator created. Please log in.\n";
    }
    while(true) {
        try {
            std::cout<<"\n=== MODERATED LMS ===\n1. Login\n0. Exit\n";
            int choice=promptInt("Choice: "); if(choice==0)break;
            auto user=app.users.authenticate(prompt("Username: "),prompt("Password: "));
            if(!user){std::cout<<"Invalid login or inactive account.\n";continue;}
            std::cout<<"\nWelcome, "<<user->fullName<<" ("<<user->roleName()<<")\n";
            bool logout=false;
            while(!logout) {
                if(user->role==Role::Administrator) {
                    std::cout<<"\n1 Create user  2 Deactivate/reactivate user  3 Create department  4 Create course  5 Enrol student  0 Logout\n";
                    int c=promptInt("Choice: ");
                    if(c==0){logout=true;continue;}
                    if(c==1){std::string un=prompt("Username: "),nm=prompt("Full name: "),pw=prompt("Temporary password (8+): ");
                        std::cout<<"Role: 1 Student, 2 Lecturer, 3 HoD, 4 Administrator\n";int r=promptInt("Role: ");
                        if(r<1||r>4)throw std::invalid_argument("Invalid role.");
                        int id=app.createUser(*user,un,nm,pw,static_cast<Role>(r-1));std::cout<<"Created user ID "<<id<<"\n";}
                    else if(c==2){for(auto& u:app.users.all())std::cout<<u->id<<" "<<u->username<<" "<<u->roleName()<<" "<<(u->active?"active":"inactive")<<"\n";
                        int id=promptInt("User ID: ");int active=promptInt("Enter 1 to activate, 0 to deactivate: ");if(active<0||active>1)throw std::invalid_argument("Use 1 or 0.");app.setUserActive(*user,id,active==1);}
                    else if(c==3){int hod=promptInt("HoD user ID: ");std::string n=prompt("Department name: ");std::cout<<"Department ID "<<app.createDepartment(*user,n,hod)<<"\n";}
                    else if(c==4){for(auto& d:app.academics.departments())std::cout<<d<<"\n";int dep=promptInt("Department ID: ");std::string code=prompt("Course code: "),n=prompt("Course name: ");int lec=promptInt("Assigned lecturer user ID: ");std::cout<<"Course ID "<<app.createCourse(*user,dep,code,n,lec)<<"\n";}
                    else if(c==5){int st=promptInt("Student user ID: "),co=promptInt("Course ID: ");app.enrolStudent(*user,st,co);std::cout<<"Student enrolled.\n";}
                    else std::cout<<"Unknown option.\n";
                } else if(user->role==Role::Student) {
                    std::cout<<"\n1 View courses  2 View assignments  3 Submit/resubmit assignment  4 View released marks  0 Logout\n";
                    int c=promptInt("Choice: ");if(c==0){logout=true;continue;}
                    if(c==1){for(auto& x:app.academics.coursesForStudent(user->id))std::cout<<x<<"\n";}
                    else if(c==2){for(auto& a:app.assignments.forStudent(user->id))std::cout<<a.id<<" | "<<a.title<<" | Deadline "<<app.assignments.applicableDeadline(a.id,user->id)<<" | Max "<<a.maxMark<<"\n";}
                    else if(c==3){int a=promptInt("Assignment ID: ");std::string path=prompt("Path to file to submit: ");int id=app.submit(*user,a,nowLocal(),path);std::cout<<"Submission saved. Submission ID "<<id<<"\n";}
                    else if(c==4){for(auto& x:app.marks.releasedForStudent(user->id))std::cout<<x<<"\n";}
                    else std::cout<<"Unknown option.\n";
                } else if(user->role==Role::Lecturer) {
                    std::cout<<"\n1 View my courses  2 Create assignment  3 Grant individual extension  4 View unmarked submissions  5 Mark submission  0 Logout\n";
                    int c=promptInt("Choice: ");if(c==0){logout=true;continue;}
                    if(c==1){for(auto& x:app.academics.coursesForLecturer(user->id))std::cout<<x<<"\n";}
                    else if(c==2){int course=promptInt("Course ID: ");std::string title=prompt("Title: "),ins=prompt("Instructions: "),dl=prompt("Deadline (YYYY-MM-DD HH:MM): ");double max=promptDouble("Maximum mark: ");std::cout<<"Assignment ID "<<app.createAssignment(*user,course,title,ins,dl,max)<<"\n";}
                    else if(c==3){int a=promptInt("Assignment ID: "),st=promptInt("Student user ID: ");std::string dl=prompt("New deadline (YYYY-MM-DD HH:MM): "),why=prompt("Reason: ");app.grantExtension(*user,a,st,dl,why);std::cout<<"Extension recorded.\n";}
                    else if(c==4){for(auto& s:app.submissions.pendingForLecturer(user->id))std::cout<<s.id<<" | Assignment "<<s.assignmentId<<" | Student "<<s.studentId<<" | "<<s.status<<" | "<<s.filePath<<"\n";}
                    else if(c==5){int sid=promptInt("Submission ID: ");double raw=promptDouble("Raw mark: ");std::string fb=prompt("Written feedback: "),why=prompt("Reason (or 'Initial marking'): ");app.markSubmission(*user,sid,raw,fb,why);std::cout<<"Mark saved for HoD moderation.\n";}
                    else std::cout<<"Unknown option.\n";
                } else if(user->role==Role::HoD) {
                    std::cout<<"\n1 View marks for moderation  2 Approve mark  3 Return mark  4 Release approved mark  0 Logout\n";
                    int c=promptInt("Choice: ");if(c==0){logout=true;continue;}
                    if(c==1){for(auto& x:app.marks.pendingForHoD(user->id))std::cout<<x<<"\n";}
                    else if(c==2||c==3){int id=promptInt("Mark ID: ");std::string reason=prompt("Reason: ");app.moderate(*user,id,c==2,reason);std::cout<<"Moderation decision saved.\n";}
                    else if(c==4){int id=promptInt("Approved mark ID: ");app.release(*user,id);std::cout<<"Mark released to student.\n";}
                    else std::cout<<"Unknown option.\n";
                }
            }
        } catch(const std::exception& e) { std::cout<<"Error: "<<e.what()<<"\n"; }
    }
    std::cout<<"Goodbye.\n";
}
} // namespace lms
