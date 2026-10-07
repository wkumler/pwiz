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


#ifndef _DATABASERUNWRITER_HPP_
#define _DATABASERUNWRITER_HPP_


#include "MSDataFile.hpp"
#include "pwiz/utility/misc/IterationListener.hpp"
#include <boost/optional.hpp>
#include <cstdint>


namespace pwiz {
namespace msdata {
namespace database {


/// The database layout shared by the SQL database formats (Serializer_DuckDB and Serializer_SQLite).
///
/// A database is meant to hold every run of an LC-MS batch: each run is appended to the database
/// (created if necessary) in a single transaction, and all rows are keyed by the run's input filename.
/// The database has five tables:
///
///   MS1       (filename, scan_idx, rt, mz, int, ion_mobility)
///   MS2       (filename, scan_idx, rt, premz, fragmz, int, voltage, ion_mobility)
///   scan_info (filename, scan_idx, native_id, ms_level, rt, polarity, centroided,
///              premz, voltage, tic, bpc, min_mz, max_mz, ion_mobility)
///   chroms    (filename, chrom_type, chrom_index, target_mz, product_mz, rt, int)
///   file_info (filename, n_scans, rt_start, rt_end, instrument, start_timestamp,
///              msconvert_version, msconvert_args, ion_mobility_type)
///
/// Retention times are in minutes and scan_idx is the 0-based spectrum index. A multiplexed MS2
/// spectrum's rows are repeated for each of its precursors. ion_mobility is NULL for data without
/// ion mobility; its kind and units (e.g. drift time in milliseconds) are in file_info.ion_mobility_type.
/// chroms follows RaMS: one row per chromatogram point, with the chromatogram id as chrom_type.


/// appends rows to one table, a value per column in table order followed by endRow()
class TableWriter
{
    public:

    virtual ~TableWriter() {}

    virtual void append(const std::string& value) = 0;
    virtual void append(int value) = 0;
    virtual void append(double value) = 0;
    virtual void append(bool value) = 0;
    virtual void appendNull() = 0;
    virtual void endRow() = 0;

    /// finishes writing rows; must be called before the transaction is committed
    virtual void close() = 0;

    template <typename T>
    void append(const boost::optional<T>& value)
    {
        if (value)
            append(*value);
        else
            appendNull();
    }

    /// appends NULL for an empty string
    void appendOrNull(const std::string& value)
    {
        if (value.empty())
            appendNull();
        else
            append(value);
    }
};

typedef boost::shared_ptr<TableWriter> TableWriterPtr;


/// an open connection to a database, implemented by each database format
class Connection
{
    public:

    virtual ~Connection() {}

    /// executes one or more SQL statements that return no result
    virtual void execute(const std::string& sql) = 0;

    /// executes a statement with filename bound to its single parameter; returns the first value of the result
    /// as an integer, or 0 if there is no result
    virtual int64_t executeWithFilename(const std::string& sql, const std::string& filename) = 0;

    /// returns a writer that appends rows to the given table
    virtual TableWriterPtr tableWriter(const std::string& table) = 0;
};


/// writes msd as a run of the database (see above), creating the tables if necessary; the run is named by
/// config.inputFilename (or else the run id), and replaces a run of the same name only if
/// config.replaceExistingDatabaseRuns is set; the columns in config.databaseIndexColumns are indexed in every
/// table that has them (no indexes by default, to keep the database small); serializerName prefixes error messages
void writeRun(Connection& connection, const std::string& databaseFilename, const MSData& msd,
              const MSDataFile::WriteConfig& config, const std::string& serializerName,
              const pwiz::util::IterationListenerRegistry* iterationListenerRegistry);


} // namespace database
} // namespace msdata
} // namespace pwiz


#endif // _DATABASERUNWRITER_HPP_
