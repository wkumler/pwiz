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

#ifndef WITHOUT_DUCKDB

#define PWIZ_SOURCE

#include "Serializer_DuckDB.hpp"
#include "DatabaseRunWriter.hpp"
#include "pwiz/utility/misc/Std.hpp"
#include "duckdb.h"

#ifdef _MSC_VER
#ifndef NOMINMAX
# define NOMINMAX
#endif
#include <windows.h> // for LoadLibraryA: duckdb.dll is delay-loaded (see libraries/duckdb/Jamfile.jam)
#endif


namespace pwiz {
namespace msdata {


using namespace pwiz::util;


namespace {


// RAII owners for DuckDB C API handles

struct Database
{
    duckdb_database db = nullptr;
    ~Database() { duckdb_close(&db); }
};

struct PreparedStatement
{
    duckdb_prepared_statement stmt = nullptr;
    ~PreparedStatement() { duckdb_destroy_prepare(&stmt); }
};

struct Result
{
    duckdb_result result;
    Result() { memset(&result, 0, sizeof(result)); }
    ~Result() { duckdb_destroy_result(&result); }
};


/// a table appender that throws on any error
class Appender : public database::TableWriter
{
    public:

    Appender(duckdb_connection con, const string& table) : table_(table)
    {
        if (duckdb_appender_create(con, nullptr, table.c_str(), &appender_) == DuckDBError)
            throwError("creating appender");
    }

    virtual ~Appender() { duckdb_appender_destroy(&appender_); }

    using database::TableWriter::append;
    virtual void append(const string& value) { check(duckdb_append_varchar_length(appender_, value.c_str(), value.length())); }
    virtual void append(int value) { check(duckdb_append_int32(appender_, value)); }
    virtual void append(double value) { check(duckdb_append_double(appender_, value)); }
    virtual void append(bool value) { check(duckdb_append_bool(appender_, value)); }
    virtual void appendNull() { check(duckdb_append_null(appender_)); }
    virtual void endRow() { check(duckdb_appender_end_row(appender_)); }

    /// flushes all rows to the table
    virtual void close()
    {
        if (duckdb_appender_close(appender_) == DuckDBError)
            throwError("writing rows");
    }

    private:

    void check(duckdb_state state)
    {
        if (state == DuckDBError)
            throwError("appending row");
    }

    void throwError(const string& action)
    {
        string message = "unknown error";
        if (appender_)
        {
            duckdb_error_data error = duckdb_appender_error_data(appender_);
            if (duckdb_error_data_message(error))
                message = duckdb_error_data_message(error);
            duckdb_destroy_error_data(&error);
        }
        throw runtime_error("[Serializer_DuckDB] error " + action + " for table " + table_ + ": " + message);
    }

    string table_;
    duckdb_appender appender_ = nullptr;
};


class DuckDBConnection : public database::Connection
{
    public:

    DuckDBConnection(duckdb_database db, const string& filename)
    {
        if (duckdb_connect(db, &con_) == DuckDBError)
            throw runtime_error("[Serializer_DuckDB::write()] unable to connect to \"" + filename + "\"");
    }

    virtual ~DuckDBConnection() { duckdb_disconnect(&con_); }

    virtual void execute(const string& sql)
    {
        Result r;
        if (duckdb_query(con_, sql.c_str(), &r.result) == DuckDBError)
            throw runtime_error("[Serializer_DuckDB] " + string(duckdb_result_error(&r.result)));
    }

    virtual int64_t executeWithFilename(const string& sql, const string& filename)
    {
        PreparedStatement p;
        if (duckdb_prepare(con_, sql.c_str(), &p.stmt) == DuckDBError)
            throw runtime_error("[Serializer_DuckDB] " + string(duckdb_prepare_error(p.stmt)));
        duckdb_bind_varchar(p.stmt, 1, filename.c_str());

        Result r;
        if (duckdb_execute_prepared(p.stmt, &r.result) == DuckDBError)
            throw runtime_error("[Serializer_DuckDB] " + string(duckdb_result_error(&r.result)));
        return duckdb_row_count(&r.result) > 0 && duckdb_column_count(&r.result) > 0 ? duckdb_value_int64(&r.result, 0, 0) : 0;
    }

    virtual database::TableWriterPtr tableWriter(const string& table)
    {
        return database::TableWriterPtr(new Appender(con_, table));
    }

    private:
    duckdb_connection con_ = nullptr;
};


} // namespace


class Serializer_DuckDB::Impl
{
    public:

    Impl(const MSDataFile::WriteConfig& config) : config_(config) {}

    void write(const string& filename, const MSData& msd,
               const IterationListenerRegistry* iterationListenerRegistry) const;

    private:
    MSDataFile::WriteConfig config_;
};


void Serializer_DuckDB::Impl::write(const string& filename, const MSData& msd,
                                    const IterationListenerRegistry* iterationListenerRegistry) const
{
#ifdef _MSC_VER
    // load the delay-loaded duckdb.dll now, so a missing DLL is a clear error instead of a crash on the first DuckDB call
    if (!LoadLibraryA("duckdb.dll"))
        throw runtime_error("[Serializer_DuckDB::write()] unable to load duckdb.dll; it must be in the same directory as the executable");
#endif

    Database db;
    char* openError = nullptr;
    if (duckdb_open_ext(filename.c_str(), &db.db, nullptr, &openError) == DuckDBError)
    {
        string message = openError ? openError : "unknown error";
        duckdb_free(openError);
        throw runtime_error("[Serializer_DuckDB::write()] unable to open \"" + filename + "\": " + message);
    }

    DuckDBConnection con(db.db, filename);
    database::writeRun(con, filename, msd, config_, "Serializer_DuckDB", iterationListenerRegistry);
}


//
// Serializer_DuckDB
//


PWIZ_API_DECL Serializer_DuckDB::Serializer_DuckDB(const MSDataFile::WriteConfig& config)
:   impl_(new Impl(config))
{}


PWIZ_API_DECL void Serializer_DuckDB::write(const string& filename, const MSData& msd,
                                            const IterationListenerRegistry* iterationListenerRegistry) const
{
    impl_->write(filename, msd, iterationListenerRegistry);
}


} // namespace msdata
} // namespace pwiz


#endif // WITHOUT_DUCKDB
