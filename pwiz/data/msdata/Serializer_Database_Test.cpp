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


#include "pwiz/utility/misc/unit.hpp"
#include "pwiz/utility/misc/Filesystem.hpp"
#include "pwiz/utility/misc/Std.hpp"
#include "MSDataFile.hpp"
#include "examples.hpp"
#include "sqlite3.h"
#ifndef WITHOUT_DUCKDB
#include "duckdb.h"
#endif

using namespace pwiz::util;
using namespace pwiz::cv;
using namespace pwiz::msdata;


// The same tests run against each database format (see DatabaseRunWriter.hpp for the tables).


ostream* os_ = 0;


/// a database format: how to write it and how to read the first value of a query result (as a string, or "NULL")
struct Backend
{
    string name;
    MSDataFile::Format format;
    string (*queryValue)(const string& dbFilename, const string& sql);
};

const Backend* backend_ = 0;


string sqliteQueryValue(const string& dbFilename, const string& sql)
{
    sqlite3* db = nullptr;
    unit_assert(sqlite3_open_v2(dbFilename.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    string value;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        value = string("ERROR: ") + sqlite3_errmsg(db);
    else if (sqlite3_step(stmt) != SQLITE_ROW || sqlite3_column_type(stmt, 0) == SQLITE_NULL)
        value = "NULL";
    else
        value = (const char*) sqlite3_column_text(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return value;
}


#ifndef WITHOUT_DUCKDB
string duckdbQueryValue(const string& dbFilename, const string& sql)
{
    duckdb_database db;
    duckdb_connection con;
    unit_assert(duckdb_open(dbFilename.c_str(), &db) == DuckDBSuccess);
    unit_assert(duckdb_connect(db, &con) == DuckDBSuccess);

    duckdb_result result;
    bool ok = duckdb_query(con, sql.c_str(), &result) == DuckDBSuccess;
    string value = !ok ? string("ERROR: ") + duckdb_result_error(&result) :
                   duckdb_value_is_null(&result, 0, 0) ? "NULL" : "";
    if (value.empty())
    {
        char* str = duckdb_value_varchar(&result, 0, 0);
        value = str;
        duckdb_free(str);
    }
    duckdb_destroy_result(&result);
    duckdb_disconnect(&con);
    duckdb_close(&db);
    return value;
}
#endif


string queryValue(const string& dbFilename, const string& sql)
{
    string value = backend_->queryValue(dbFilename, sql);
    if (os_)
        *os_ << backend_->name << ": " << sql << " -> " << value << endl;
    return value;
}

int64_t queryCount(const string& dbFilename, const string& sql)
{
    return lexical_cast<int64_t>(queryValue(dbFilename, sql));
}


/// counts the data points in the spectra of the given MS level
size_t countPoints(const MSData& msd, int msLevel)
{
    size_t count = 0;
    SpectrumList& sl = *msd.run.spectrumListPtr;
    for (size_t i = 0; i < sl.size(); ++i)
    {
        SpectrumPtr s = sl.spectrum(i, true);
        if (s->cvParam(MS_ms_level).valueAs<int>() == msLevel)
            count += s->getMZArray() ? s->getMZArray()->data.size() : 0;
    }
    return count;
}


void write(const MSData& msd, const string& dbFilename, const string& runName, bool replaceRuns = false)
{
    MSDataFile::WriteConfig config(backend_->format);
    config.inputFilename = runName;
    config.replaceExistingDatabaseRuns = replaceRuns;
    config.useWorkerThreads = false;
    MSDataFile::write(msd, dbFilename, config);
}


/// a spectrum list that fails partway through, to test that a failed run is rolled back
struct FailingSpectrumList : public SpectrumListSimple
{
    virtual SpectrumPtr spectrum(size_t index, bool getBinaryData) const
    {
        if (index > 0)
            throw runtime_error("simulated read failure");
        return SpectrumListSimple::spectrum(index, getBinaryData);
    }
};


void testMultiplexingAndIonMobility(const string& dbFilename);


void test(const string& dbFilename)
{
    MSData tiny;
    examples::initializeTiny(tiny);
    int64_t ms1Points = countPoints(tiny, 1), ms2Points = countPoints(tiny, 2);
    int64_t scanCount = tiny.run.spectrumListPtr->size();
    unit_assert(ms1Points > 0 && ms2Points > 0);

    // first run creates the database
    write(tiny, dbFilename, "tiny1.mzML");
    unit_assert_operator_equal(ms1Points, queryCount(dbFilename, "SELECT count(*) FROM MS1"));
    unit_assert_operator_equal(ms2Points, queryCount(dbFilename, "SELECT count(*) FROM MS2"));
    unit_assert_operator_equal(scanCount, queryCount(dbFilename, "SELECT count(*) FROM scan_info"));
    unit_assert_operator_equal((int64_t) 1, queryCount(dbFilename, "SELECT count(*) FROM file_info"));
    unit_assert_operator_equal("tiny1.mzML", queryValue(dbFilename, "SELECT DISTINCT filename FROM MS1"));

    // spot-check values against the tiny example: the first MS1 scan starts at 5.890500 minutes,
    // its m/z values are 0..14 with intensities 15..1, and the first MS2 scan's (index 1) isolation window target is m/z 445.3
    unit_assert_operator_equal("5.8905", queryValue(dbFilename, "SELECT round(rt, 6) FROM MS1 WHERE scan_idx = 0 LIMIT 1"));
    unit_assert_operator_equal("15.0", queryValue(dbFilename, "SELECT \"int\" FROM MS1 WHERE scan_idx = 0 AND mz = 0"));
    unit_assert_operator_equal("445.3", queryValue(dbFilename, "SELECT DISTINCT premz FROM MS2 WHERE scan_idx = 1"));
    unit_assert_operator_equal("120.0", queryValue(dbFilename, "SELECT tic FROM scan_info WHERE scan_idx = 0"));
    unit_assert_operator_equal("positive", queryValue(dbFilename, "SELECT polarity FROM scan_info WHERE scan_idx = 0"));
    unit_assert_operator_equal(scanCount, queryCount(dbFilename, "SELECT n_scans FROM file_info"));
    unit_assert_operator_equal((int64_t) 0, queryCount(dbFilename, "SELECT count(ion_mobility) FROM MS1"));
    unit_assert_operator_equal("NULL", queryValue(dbFilename, "SELECT ion_mobility_type FROM file_info"));

    // the tiny example's chromatograms: "tic" with 15 points and "sic" with 10 points (times in seconds),
    // the latter with precursor and product isolation window targets 456.7 and 678.9
    unit_assert_operator_equal((int64_t) 25, queryCount(dbFilename, "SELECT count(*) FROM chroms"));
    unit_assert_operator_equal("456.7", queryValue(dbFilename, "SELECT DISTINCT target_mz FROM chroms WHERE chrom_type = 'sic'"));
    unit_assert_operator_equal("678.9", queryValue(dbFilename, "SELECT DISTINCT product_mz FROM chroms WHERE chrom_type = 'sic'"));
    unit_assert_operator_equal("0.15", queryValue(dbFilename, "SELECT rt FROM chroms WHERE chrom_type = 'sic' AND \"int\" = 1"));
    unit_assert_operator_equal("NULL", queryValue(dbFilename, "SELECT DISTINCT target_mz FROM chroms WHERE chrom_type = 'tic'"));

    // a second run is appended
    write(tiny, dbFilename, "tiny2.mzML");
    unit_assert_operator_equal(2 * ms1Points, queryCount(dbFilename, "SELECT count(*) FROM MS1"));
    unit_assert_operator_equal((int64_t) 2, queryCount(dbFilename, "SELECT count(*) FROM file_info"));

    // writing a run that is already in the database is an error and changes nothing
    unit_assert_throws(write(tiny, dbFilename, "tiny1.mzML"), user_error);
    unit_assert_operator_equal(2 * ms1Points, queryCount(dbFilename, "SELECT count(*) FROM MS1"));
    unit_assert_operator_equal((int64_t) 2, queryCount(dbFilename, "SELECT count(*) FROM file_info"));

    // ...unless replacing runs
    write(tiny, dbFilename, "tiny1.mzML", true);
    unit_assert_operator_equal(2 * ms1Points, queryCount(dbFilename, "SELECT count(*) FROM MS1"));
    unit_assert_operator_equal(2 * scanCount, queryCount(dbFilename, "SELECT count(*) FROM scan_info"));
    unit_assert_operator_equal((int64_t) 50, queryCount(dbFilename, "SELECT count(*) FROM chroms"));
    unit_assert_operator_equal((int64_t) 2, queryCount(dbFilename, "SELECT count(*) FROM file_info"));

    // a run that fails partway through leaves no rows behind
    MSData failing;
    examples::initializeTiny(failing);
    shared_ptr<FailingSpectrumList> failingList(new FailingSpectrumList);
    failingList->spectra = boost::dynamic_pointer_cast<SpectrumListSimple>(tiny.run.spectrumListPtr)->spectra;
    failing.run.spectrumListPtr = failingList;
    unit_assert_throws_what(write(failing, dbFilename, "failing.mzML"), runtime_error, "simulated read failure");
    unit_assert_operator_equal((int64_t) 0, queryCount(dbFilename, "SELECT count(*) FROM scan_info WHERE filename = 'failing.mzML'"));
    unit_assert_operator_equal((int64_t) 0, queryCount(dbFilename, "SELECT count(*) FROM chroms WHERE filename = 'failing.mzML'"));
    unit_assert_operator_equal(2 * ms1Points, queryCount(dbFilename, "SELECT count(*) FROM MS1"));

    testMultiplexingAndIonMobility(dbFilename);
}


/// a multiplexed MS2 spectrum gets rows for each precursor; ion mobility comes from a spectrum's single value or its array
void testMultiplexingAndIonMobility(const string& dbFilename)
{
    MSData msd;
    examples::initializeTiny(msd);
    vector<SpectrumPtr>& spectra = boost::dynamic_pointer_cast<SpectrumListSimple>(msd.run.spectrumListPtr)->spectra;

    // spectrum 1 (10 points, precursor 445.3) gets a second precursor
    Precursor secondPrecursor;
    secondPrecursor.isolationWindow.set(MS_isolation_window_target_m_z, 600.5, MS_m_z);
    spectra[1]->precursors.push_back(secondPrecursor);

    // spectrum 0 (MS1, 15 points) gets a single inverse reduced ion mobility value
    spectra[0]->scanList.scans[0].set(MS_inverse_reduced_ion_mobility, 0.85, MS_volt_second_per_square_centimeter);

    // spectrum 3 (MS2, 10 points) gets an ion mobility array of 0, 0.5, ..., 4.5 ms
    BinaryDataArrayPtr ionMobilityArray(new BinaryDataArray);
    ionMobilityArray->set(MS_raw_ion_mobility_array, "", UO_millisecond);
    for (int i = 0; i < 10; ++i)
        ionMobilityArray->data.push_back(i * 0.5);
    spectra[3]->binaryDataArrayPtrs.push_back(ionMobilityArray);

    write(msd, dbFilename, "variants.mzML");
    const string run = " FROM MS2 WHERE filename = 'variants.mzML' AND scan_idx = ";
    unit_assert_operator_equal((int64_t) 20, queryCount(dbFilename, "SELECT count(*)" + run + "1"));
    unit_assert_operator_equal((int64_t) 2, queryCount(dbFilename, "SELECT count(DISTINCT premz)" + run + "1"));
    unit_assert_operator_equal("600.5", queryValue(dbFilename, "SELECT max(premz)" + run + "1"));
    unit_assert_operator_equal("445.3", queryValue(dbFilename, "SELECT premz FROM scan_info WHERE filename = 'variants.mzML' AND scan_idx = 1"));

    unit_assert_operator_equal((int64_t) 15, queryCount(dbFilename, "SELECT count(*) FROM MS1 WHERE filename = 'variants.mzML' AND ion_mobility = 0.85"));
    unit_assert_operator_equal("0.85", queryValue(dbFilename, "SELECT ion_mobility FROM scan_info WHERE filename = 'variants.mzML' AND scan_idx = 0"));
    unit_assert_operator_equal((int64_t) 10, queryCount(dbFilename, "SELECT count(DISTINCT ion_mobility)" + run + "3"));
    unit_assert_operator_equal("4.5", queryValue(dbFilename, "SELECT max(ion_mobility)" + run + "3"));
    unit_assert_operator_equal("NULL", queryValue(dbFilename, "SELECT ion_mobility FROM scan_info WHERE filename = 'variants.mzML' AND scan_idx = 3"));
    unit_assert_operator_equal("inverse reduced ion mobility (volt-second per square centimeter)",
                               queryValue(dbFilename, "SELECT ion_mobility_type FROM file_info WHERE filename = 'variants.mzML'"));
}


int main(int argc, char* argv[])
{
    TEST_PROLOG(argc, argv)

    vector<Backend> backends;
    backends.push_back(Backend{"SQLite", MSDataFile::Format_SQLite, &sqliteQueryValue});
#ifndef WITHOUT_DUCKDB
    backends.push_back(Backend{"DuckDB", MSDataFile::Format_DuckDB, &duckdbQueryValue});
#endif

    if (argc>1 && !strcmp(argv[1],"-v")) os_ = &cout;
    for (const Backend& backend : backends)
    {
        backend_ = &backend;
        string dbFilename = (bfs::temp_directory_path() / bfs::unique_path("Serializer_Database_Test-%%%%%%.db")).string();
        try
        {
            test(dbFilename);
        }
        catch (exception& e)
        {
            TEST_FAILED(backend.name + ": " + e.what())
        }
        catch (...)
        {
            TEST_FAILED(backend.name + ": caught unknown exception.")
        }
        bfs::remove(dbFilename);
        bfs::remove(dbFilename + ".wal");
        bfs::remove(dbFilename + "-journal");
    }

    TEST_EPILOG
}
