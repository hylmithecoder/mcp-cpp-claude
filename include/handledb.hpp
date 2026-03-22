#include <sqlite3.h>
#include <iostream>
#include <string>
using namespace std;

namespace Tools {
    class DataBase{
    public:
        sqlite3 *db = nullptr;
        int rc;

        DataBase(const char* dbPath){
            initDb(dbPath);
        }

        void initDb(const char* dbPath);

        ~DataBase();
    };
}