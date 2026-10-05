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


#ifndef _SERIALIZER_DUCKDB_HPP_
#define _SERIALIZER_DUCKDB_HPP_


#include "MSDataFile.hpp"
#include "pwiz/utility/misc/Export.hpp"
#include "pwiz/utility/misc/IterationListener.hpp"


namespace pwiz {
namespace msdata {


/// MSData -> DuckDB database serialization (write only).
///
/// Unlike the other formats, a DuckDB database is meant to hold every run of an LC-MS batch:
/// each run is appended to the database (created if necessary) in a single transaction, and
/// all rows are keyed by the run's input filename. The database has four tables:
///
///   MS1       (filename, scan_idx, rt, mz, int)
///   MS2       (filename, scan_idx, rt, premz, fragmz, int, voltage)
///   scan_info (filename, scan_idx, native_id, ms_level, rt, polarity, centroided,
///              premz, voltage, tic, bpc, min_mz, max_mz)
///   file_info (filename, n_scans, rt_start, rt_end, instrument, start_timestamp,
///              msconvert_version, filters)
///
/// Retention times are in minutes and scan_idx is the 0-based spectrum index.
class PWIZ_API_DECL Serializer_DuckDB
{
    public:

    /// constructs a serializer using the inputFilename and duckdbReplaceRuns settings of config
    Serializer_DuckDB(const MSDataFile::WriteConfig& config);

    /// appends msd to the DuckDB database at filename, creating the database if it does not exist;
    /// throws if a run with the same filename is already in the database (unless replacing runs)
    void write(const std::string& filename, const MSData& msd,
               const pwiz::util::IterationListenerRegistry* iterationListenerRegistry = 0) const;

    private:
    class Impl;
    boost::shared_ptr<Impl> impl_;
    Serializer_DuckDB(Serializer_DuckDB&);
    Serializer_DuckDB& operator=(Serializer_DuckDB&);
};


} // namespace msdata
} // namespace pwiz


#endif // _SERIALIZER_DUCKDB_HPP_
