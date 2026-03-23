#pragma once
#include <sqlite3.h>
#include <iostream>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

using namespace std;
using json = nlohmann::json;

struct FormatInput {
    std::string context;
    std::string response;
    std::string timestamp;
};

namespace Tools {
    class DataBase{
    public:
        sqlite3 *db = nullptr;
        int rc;

        DataBase(const char* dbPath){
            initDb(dbPath);
        }

        void initDb(const char* dbPath);

        int insertData(FormatInput data, sqlite3* db);

        string readTheContext(sqlite3* db, int idTitle);
        ~DataBase();
    };
}