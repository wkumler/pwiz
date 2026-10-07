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


#ifndef _SERIALIZER_SQLITE_HPP_
#define _SERIALIZER_SQLITE_HPP_


#include "MSDataFile.hpp"
#include "pwiz/utility/misc/Export.hpp"
#include "pwiz/utility/misc/IterationListener.hpp"


namespace pwiz {
namespace msdata {


/// MSData -> SQLite database serialization (write only).
///
/// Unlike the other formats, a SQLite database is meant to hold every run of an LC-MS batch:
/// each run is appended to the database (created if necessary) in a single transaction, and
/// all rows are keyed by the run's input filename. See DatabaseRunWriter.hpp for the tables.
class PWIZ_API_DECL Serializer_SQLite
{
    public:

    /// constructs a serializer using the inputFilename and replaceExistingDatabaseRuns settings of config
    Serializer_SQLite(const MSDataFile::WriteConfig& config);

    /// appends msd to the SQLite database at filename, creating the database if it does not exist;
    /// throws if a run with the same filename is already in the database (unless replacing runs)
    void write(const std::string& filename, const MSData& msd,
               const pwiz::util::IterationListenerRegistry* iterationListenerRegistry = 0) const;

    private:
    class Impl;
    boost::shared_ptr<Impl> impl_;
    Serializer_SQLite(Serializer_SQLite&);
    Serializer_SQLite& operator=(Serializer_SQLite&);
};


} // namespace msdata
} // namespace pwiz


#endif // _SERIALIZER_SQLITE_HPP_
