//
// $Id$
//
//
// Original author: William Kumler <wkumler .at. uw.edu>
// AI assistance: Claude Code (Claude Opus 5.5) <noreply .at. anthropic.com>
//
// Copyright 2026 William Kumler
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
#include "SpectrumWorkerThreads.hpp"
#include "pwiz/Version.hpp"
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


const char* createTablesSql =
    "CREATE TABLE IF NOT EXISTS MS1 (filename VARCHAR, scan_idx INTEGER, rt DOUBLE, mz DOUBLE, \"int\" DOUBLE);"
    "CREATE TABLE IF NOT EXISTS MS2 (filename VARCHAR, scan_idx INTEGER, rt DOUBLE, premz DOUBLE, fragmz DOUBLE, \"int\" DOUBLE, voltage DOUBLE);"
    "CREATE TABLE IF NOT EXISTS scan_info (filename VARCHAR, scan_idx INTEGER, native_id VARCHAR, ms_level INTEGER, rt DOUBLE,"
    "    polarity VARCHAR, centroided BOOLEAN, premz DOUBLE, voltage DOUBLE, tic DOUBLE, bpc DOUBLE, min_mz DOUBLE, max_mz DOUBLE);"
    "CREATE TABLE IF NOT EXISTS file_info (filename VARCHAR, n_scans INTEGER, rt_start DOUBLE, rt_end DOUBLE, instrument VARCHAR,"
    "    start_timestamp VARCHAR, msconvert_version VARCHAR, msconvert_args VARCHAR);";

const char* tableNames[] = {"MS1", "MS2", "scan_info", "file_info"};


// RAII owners for DuckDB C API handles

struct Database
{
    duckdb_database db = nullptr;
    ~Database() { duckdb_close(&db); }
};

struct Connection
{
    duckdb_connection con = nullptr;
    ~Connection() { duckdb_disconnect(&con); }
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
class Appender
{
    public:

    Appender(duckdb_connection con, const char* table) : table_(table)
    {
        if (duckdb_appender_create(con, nullptr, table, &appender_) == DuckDBError)
            throwError("creating appender");
    }

    ~Appender() { duckdb_appender_destroy(&appender_); }

    void append(const string& value) { check(duckdb_append_varchar_length(appender_, value.c_str(), value.length())); }
    void append(int value) { check(duckdb_append_int32(appender_, value)); }
    void append(double value) { check(duckdb_append_double(appender_, value)); }
    void append(bool value) { check(duckdb_append_bool(appender_, value)); }
    void appendNull() { check(duckdb_append_null(appender_)); }

    template <typename T>
    void append(const boost::optional<T>& value)
    {
        if (value)
            append(*value);
        else
            appendNull();
    }

    /// appends NULL for an empty string
    void appendOrNull(const string& value)
    {
        if (value.empty())
            appendNull();
        else
            append(value);
    }

    void endRow() { check(duckdb_appender_end_row(appender_)); }

    /// flushes all rows to the table; must be called before committing
    void close()
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


void execute(duckdb_connection con, const string& sql)
{
    Result r;
    if (duckdb_query(con, sql.c_str(), &r.result) == DuckDBError)
        throw runtime_error("[Serializer_DuckDB] " + string(duckdb_result_error(&r.result)));
}


/// executes a statement with a single string parameter and returns the first value of the result as an integer (or 0 if no rows)
int64_t executeWithFilename(duckdb_connection con, const string& sql, const string& filename)
{
    PreparedStatement p;
    if (duckdb_prepare(con, sql.c_str(), &p.stmt) == DuckDBError)
        throw runtime_error("[Serializer_DuckDB] " + string(duckdb_prepare_error(p.stmt)));
    duckdb_bind_varchar(p.stmt, 1, filename.c_str());

    Result r;
    if (duckdb_execute_prepared(p.stmt, &r.result) == DuckDBError)
        throw runtime_error("[Serializer_DuckDB] " + string(duckdb_result_error(&r.result)));
    return duckdb_row_count(&r.result) > 0 && duckdb_column_count(&r.result) > 0 ? duckdb_value_int64(&r.result, 0, 0) : 0;
}


template <typename T>
boost::optional<T> optionalValue(const CVParam& param)
{
    if (param.empty())
        return boost::none;
    return param.valueAs<T>();
}


string instrumentModel(const MSData& msd)
{
    for (const InstrumentConfigurationPtr& ic : msd.instrumentConfigurationPtrs)
    {
        if (!ic)
            continue;
        CVParam model = ic->cvParamChild(MS_instrument_model);
        if (!model.empty() && model.cvid != MS_instrument_model)
            return model.name();
        UserParam msModel = ic->userParam("msModel");
        if (!msModel.empty())
            return msModel.value;
    }
    return "";
}


string commandLineArgs(const MSData& msd)
{
    for (const DataProcessingPtr& dp : msd.allDataProcessingPtrs())
    {
        if (!dp)
            continue;
        for (const ProcessingMethod& pm : dp->processingMethods)
        {
            CVParam args = pm.cvParam(MS_command_line_parameters);
            if (!args.empty())
                return args.value;
        }
    }
    return "";
}


} // namespace


class Serializer_DuckDB::Impl
{
    public:

    Impl(const MSDataFile::WriteConfig& config) : config_(config) {}

    void write(const string& filename, const MSData& msd,
               const IterationListenerRegistry* iterationListenerRegistry) const;

    private:

    void writeRun(duckdb_connection con, const string& runName, const MSData& msd,
                  const IterationListenerRegistry* iterationListenerRegistry) const;

    MSDataFile::WriteConfig config_;
};


void Serializer_DuckDB::Impl::write(const string& filename, const MSData& msd,
                                    const IterationListenerRegistry* iterationListenerRegistry) const
{
    string runName = config_.inputFilename.empty() ? msd.run.id : config_.inputFilename;
    if (runName.empty())
        throw runtime_error("[Serializer_DuckDB::write()] no input filename or run id to identify the run by");

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

    Connection con;
    if (duckdb_connect(db.db, &con.con) == DuckDBError)
        throw runtime_error("[Serializer_DuckDB::write()] unable to connect to \"" + filename + "\"");

    execute(con.con, createTablesSql);

    // the whole run is written in one transaction, so a failed conversion leaves the database as it was
    execute(con.con, "BEGIN TRANSACTION");
    try
    {
        if (executeWithFilename(con.con, "SELECT count(*) FROM file_info WHERE filename = ?", runName) > 0)
        {
            if (!config_.duckdbReplaceRuns)
                throw user_error("[Serializer_DuckDB::write()] \"" + runName + "\" is already in " + filename +
                                 "; use --duckdbReplaceRuns to replace it");

            for (const char* table : tableNames)
                executeWithFilename(con.con, string("DELETE FROM ") + table + " WHERE filename = ?", runName);
        }

        writeRun(con.con, runName, msd, iterationListenerRegistry);
        execute(con.con, "COMMIT");
    }
    catch (...)
    {
        duckdb_result r;
        duckdb_query(con.con, "ROLLBACK", &r);
        duckdb_destroy_result(&r);
        throw;
    }
}


void Serializer_DuckDB::Impl::writeRun(duckdb_connection con, const string& runName, const MSData& msd,
                                       const IterationListenerRegistry* iterationListenerRegistry) const
{
    Appender ms1(con, "MS1"), ms2(con, "MS2"), scanInfo(con, "scan_info");

    int scansWritten = 0;
    boost::optional<double> rtStart, rtEnd;

    SpectrumList& sl = *msd.run.spectrumListPtr;
    SpectrumWorkerThreads spectrumWorkers(sl, config_.useWorkerThreads, config_.continueOnError);
    for (size_t i = 0, end = sl.size(); i < end; ++i)
    {
        SpectrumPtr s;
        try
        {
            s = spectrumWorkers.processBatch(i);
        }
        catch (std::exception& e)
        {
            if (config_.continueOnError)
            {
                cerr << "Skipping spectrum " << i << " \"" << sl.spectrumIdentity(i).id << "\": " << e.what() << endl;
                continue;
            }
            else
                throw;
        }

        int scanIndex = (int) s->index;
        boost::optional<int> msLevel = optionalValue<int>(s->cvParam(MS_ms_level));

        boost::optional<double> rt;
        if (!s->scanList.empty())
        {
            CVParam scanTime = s->scanList.scans[0].cvParam(MS_scan_start_time);
            if (!scanTime.empty())
                rt = scanTime.timeInSeconds() / 60.0;
        }
        if (rt)
        {
            rtStart = rtStart ? min(*rtStart, *rt) : *rt;
            rtEnd = rtEnd ? max(*rtEnd, *rt) : *rt;
        }

        boost::optional<string> polarity;
        if (s->hasCVParam(MS_positive_scan))
            polarity = string("positive");
        else if (s->hasCVParam(MS_negative_scan))
            polarity = string("negative");

        boost::optional<bool> centroided;
        if (s->hasCVParam(MS_centroid_spectrum))
            centroided = true;
        else if (s->hasCVParam(MS_profile_spectrum))
            centroided = false;

        boost::optional<double> premz, voltage;
        if (msLevel && *msLevel > 1 && !s->precursors.empty())
        {
            const Precursor& precursor = s->precursors[0];
            premz = optionalValue<double>(precursor.isolationWindow.cvParam(MS_isolation_window_target_m_z));
            if (!premz && !precursor.selectedIons.empty())
                premz = optionalValue<double>(precursor.selectedIons[0].cvParam(MS_selected_ion_m_z));
            voltage = optionalValue<double>(precursor.activation.cvParam(MS_collision_energy));
        }

        BinaryDataArrayPtr mzArray = s->getMZArray();
        BinaryDataArrayPtr intensityArray = s->getIntensityArray();
        size_t pointCount = mzArray && intensityArray ? min(mzArray->data.size(), intensityArray->data.size()) : 0;

        boost::optional<double> tic, bpc, minMz, maxMz;
        for (size_t p = 0; p < pointCount; ++p)
        {
            double mz = mzArray->data[p], intensity = intensityArray->data[p];
            tic = tic.value_or(0) + intensity;
            bpc = bpc ? max(*bpc, intensity) : intensity;
            minMz = minMz ? min(*minMz, mz) : mz;
            maxMz = maxMz ? max(*maxMz, mz) : mz;

            if (msLevel && *msLevel == 1)
            {
                ms1.append(runName); ms1.append(scanIndex); ms1.append(rt);
                ms1.append(mz); ms1.append(intensity);
                ms1.endRow();
            }
            else if (msLevel && *msLevel == 2)
            {
                ms2.append(runName); ms2.append(scanIndex); ms2.append(rt); ms2.append(premz);
                ms2.append(mz); ms2.append(intensity); ms2.append(voltage);
                ms2.endRow();
            }
        }

        scanInfo.append(runName); scanInfo.append(scanIndex); scanInfo.append(s->id); scanInfo.append(msLevel);
        scanInfo.append(rt); scanInfo.append(polarity); scanInfo.append(centroided); scanInfo.append(premz);
        scanInfo.append(voltage); scanInfo.append(tic); scanInfo.append(bpc); scanInfo.append(minMz);
        scanInfo.append(maxMz);
        scanInfo.endRow();
        ++scansWritten;

        // update any listeners and handle cancellation
        if (iterationListenerRegistry &&
            iterationListenerRegistry->broadcastUpdateMessage(IterationListener::UpdateMessage(i, end)) == IterationListener::Status_Cancel)
            throw runtime_error("[Serializer_DuckDB::write()] conversion cancelled");
    }

    ms1.close();
    ms2.close();
    scanInfo.close();

    Appender fileInfo(con, "file_info");
    fileInfo.append(runName);
    fileInfo.append(scansWritten);
    fileInfo.append(rtStart);
    fileInfo.append(rtEnd);

    fileInfo.appendOrNull(instrumentModel(msd));
    fileInfo.appendOrNull(msd.run.startTimeStamp);
    fileInfo.append(pwiz::Version::str());
    fileInfo.appendOrNull(commandLineArgs(msd));
    fileInfo.endRow();
    fileInfo.close();
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
