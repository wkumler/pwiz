//
// $Id$
//
//
// Original author: William Kumler <wkumler .at. uw.edu>
// AI assistance: Claude Code (Claude Opus 5.5) <noreply .at. anthropic.com>
//
// Copyright 2026 University of Washington - Seattle, WA
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#define PWIZ_SOURCE

#include "Serializer_SQLite.hpp"
#include "DatabaseRunWriter.hpp"
#include "pwiz/utility/misc/Std.hpp"
#include "sqlite3.h"


namespace pwiz {
namespace msdata {


using namespace pwiz::util;


namespace {


/// a prepared statement that is finalized when destroyed
class Statement
{
    public:

    Statement(sqlite3* db, const string& sql) : db_(db)
    {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK)
            throwError("preparing \"" + sql + "\"");
    }

    ~Statement() { sqlite3_finalize(stmt_); }

    sqlite3_stmt* get() const { return stmt_; }

    void check(int result, const string& action) const
    {
        if (result != SQLITE_OK)
            throwError(action);
    }

    void throwError(const string& action) const
    {
        throw runtime_error("[Serializer_SQLite] error " + action + ": " + sqlite3_errmsg(db_));
    }

    private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};


/// inserts rows into a table with a prepared INSERT statement, binding a value per column
class Inserter : public database::TableWriter
{
    public:

    Inserter(sqlite3* db, const string& table) : table_(table), columnCount_(countColumns(db, table)),
        insert_(db, "INSERT INTO \"" + table + "\" VALUES (" + parameters(columnCount_) + ")")
    {}

    using database::TableWriter::append;
    virtual void append(const string& value) { bind(sqlite3_bind_text(insert_.get(), nextColumn(), value.c_str(), (int) value.length(), SQLITE_TRANSIENT)); }
    virtual void append(int value) { bind(sqlite3_bind_int(insert_.get(), nextColumn(), value)); }
    virtual void append(double value) { bind(sqlite3_bind_double(insert_.get(), nextColumn(), value)); }
    virtual void append(bool value) { bind(sqlite3_bind_int(insert_.get(), nextColumn(), value ? 1 : 0)); }
    virtual void appendNull() { bind(sqlite3_bind_null(insert_.get(), nextColumn())); }

    virtual void endRow()
    {
        if (column_ != columnCount_)
            throw runtime_error("[Serializer_SQLite] row for table " + table_ + " has " + lexical_cast<string>(column_) +
                                " values but the table has " + lexical_cast<string>(columnCount_) + " columns");
        if (sqlite3_step(insert_.get()) != SQLITE_DONE)
            insert_.throwError("inserting row into table " + table_);
        sqlite3_reset(insert_.get());
        column_ = 0;
    }

    /// rows are inserted as they are ended, so there is nothing left to write
    virtual void close() {}

    private:

    static int countColumns(sqlite3* db, const string& table)
    {
        Statement select(db, "SELECT * FROM \"" + table + "\" LIMIT 0");
        return sqlite3_column_count(select.get());
    }

    static string parameters(int count)
    {
        string result;
        for (int i = 0; i < count; ++i)
            result += i == 0 ? "?" : ", ?";
        return result;
    }

    int nextColumn()
    {
        if (column_ >= columnCount_)
            throw runtime_error("[Serializer_SQLite] too many values for a row of table " + table_);
        return ++column_; // parameters are numbered from 1
    }

    void bind(int result) { insert_.check(result, "binding value for table " + table_); }

    string table_;
    int columnCount_;
    int column_ = 0;
    Statement insert_;
};


class SQLiteConnection : public database::Connection
{
    public:

    SQLiteConnection(const string& filename)
    {
        // filename is UTF-8, as sqlite3_open_v2 expects
        if (sqlite3_open_v2(filename.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        {
            string message = db_ ? sqlite3_errmsg(db_) : "out of memory";
            sqlite3_close(db_);
            throw runtime_error("[Serializer_SQLite::write()] unable to open \"" + filename + "\": " + message);
        }
    }

    virtual ~SQLiteConnection() { sqlite3_close(db_); }

    virtual void execute(const string& sql)
    {
        char* error = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK)
        {
            string message = error ? error : sqlite3_errmsg(db_);
            sqlite3_free(error);
            throw runtime_error("[Serializer_SQLite] " + message);
        }
    }

    virtual int64_t executeWithFilename(const string& sql, const string& filename)
    {
        Statement statement(db_, sql);
        statement.check(sqlite3_bind_text(statement.get(), 1, filename.c_str(), (int) filename.length(), SQLITE_TRANSIENT), "binding filename");

        int result = sqlite3_step(statement.get());
        if (result == SQLITE_ROW)
            return sqlite3_column_int64(statement.get(), 0);
        if (result != SQLITE_DONE)
            statement.throwError("executing \"" + sql + "\"");
        return 0;
    }

    virtual database::TableWriterPtr tableWriter(const string& table)
    {
        return database::TableWriterPtr(new Inserter(db_, table));
    }

    private:
    sqlite3* db_ = nullptr;
};


} // namespace


class Serializer_SQLite::Impl
{
    public:

    Impl(const MSDataFile::WriteConfig& config) : config_(config) {}

    void write(const string& filename, const MSData& msd,
               const IterationListenerRegistry* iterationListenerRegistry) const
    {
        SQLiteConnection con(filename);
        database::writeRun(con, filename, msd, config_, "Serializer_SQLite", iterationListenerRegistry);
    }

    private:
    MSDataFile::WriteConfig config_;
};


//
// Serializer_SQLite
//


PWIZ_API_DECL Serializer_SQLite::Serializer_SQLite(const MSDataFile::WriteConfig& config)
:   impl_(new Impl(config))
{}


PWIZ_API_DECL void Serializer_SQLite::write(const string& filename, const MSData& msd,
                                            const IterationListenerRegistry* iterationListenerRegistry) const
{
    impl_->write(filename, msd, iterationListenerRegistry);
}


} // namespace msdata
} // namespace pwiz
