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

#include "DatabaseRunWriter.hpp"
#include "SpectrumWorkerThreads.hpp"
#include "pwiz/Version.hpp"
#include "pwiz/utility/misc/Std.hpp"


namespace pwiz {
namespace msdata {
namespace database {


using namespace pwiz::util;


namespace {


// the column types are understood by both DuckDB and SQLite (which stores BOOLEAN as 0 or 1)
const char* createTablesSql =
    "CREATE TABLE IF NOT EXISTS MS1 (filename VARCHAR, scan_idx INTEGER, rt DOUBLE, mz DOUBLE, \"int\" DOUBLE, ion_mobility DOUBLE);"
    "CREATE TABLE IF NOT EXISTS MS2 (filename VARCHAR, scan_idx INTEGER, rt DOUBLE, premz DOUBLE, fragmz DOUBLE, \"int\" DOUBLE, voltage DOUBLE,"
    "    ion_mobility DOUBLE);"
    "CREATE TABLE IF NOT EXISTS scan_info (filename VARCHAR, scan_idx INTEGER, native_id VARCHAR, ms_level INTEGER, rt DOUBLE,"
    "    polarity VARCHAR, centroided BOOLEAN, premz DOUBLE, voltage DOUBLE, tic DOUBLE, bpc DOUBLE, min_mz DOUBLE, max_mz DOUBLE,"
    "    ion_mobility DOUBLE);"
    "CREATE TABLE IF NOT EXISTS chroms (filename VARCHAR, chrom_type VARCHAR, chrom_index INTEGER, target_mz DOUBLE, product_mz DOUBLE,"
    "    rt DOUBLE, \"int\" DOUBLE);"
    "CREATE TABLE IF NOT EXISTS file_info (filename VARCHAR, n_scans INTEGER, rt_start DOUBLE, rt_end DOUBLE, instrument VARCHAR,"
    "    start_timestamp VARCHAR, msconvert_version VARCHAR, msconvert_args VARCHAR, ion_mobility_type VARCHAR);";

const char* tableNames[] = {"MS1", "MS2", "scan_info", "chroms", "file_info"};


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


/// a precursor's m/z (its isolation window target, or else its first selected ion) and collision energy
struct PrecursorInfo
{
    boost::optional<double> mz, voltage;
};

PrecursorInfo precursorInfo(const Precursor& precursor)
{
    PrecursorInfo info;
    info.mz = optionalValue<double>(precursor.isolationWindow.cvParam(MS_isolation_window_target_m_z));
    if (!info.mz && !precursor.selectedIons.empty())
        info.mz = optionalValue<double>(precursor.selectedIons[0].cvParam(MS_selected_ion_m_z));
    info.voltage = optionalValue<double>(precursor.activation.cvParam(MS_collision_energy));
    return info;
}


/// a spectrum's single ion mobility value (drift time, inverse reduced ion mobility or FAIMS compensation voltage),
/// or an empty CVParam if it has none (it may instead have an ion mobility array)
CVParam spectrumIonMobility(const Spectrum& s)
{
    const CVID types[] = {MS_ion_mobility_drift_time, MS_inverse_reduced_ion_mobility, MS_FAIMS_compensation_voltage};
    for (CVID type : types)
    {
        CVParam param = s.scanList.empty() ? CVParam() : s.scanList.scans[0].cvParam(type);
        if (param.empty())
            param = s.cvParam(type);
        if (!param.empty())
            return param;
    }
    return CVParam();
}


/// describes an ion mobility value or array, e.g. "inverse reduced ion mobility (volt-second per square centimeter)"
string ionMobilityType(const CVParam& param)
{
    return param.unitsName().empty() ? param.name() : param.name() + " (" + param.unitsName() + ")";
}


/// the factor that converts a time array in the given units to minutes, or 0 if the units are not a time unit
double minutesPerTimeUnit(CVID units)
{
    return CVParam(MS_scan_start_time, 1.0, units).timeInSeconds() / 60.0;
}


/// writes every chromatogram point (as in RaMS's chroms table: one row per point, with the chromatogram's
/// id as chrom_type and its precursor and product isolation window targets as target_mz and product_mz)
void writeChromatograms(Connection& connection, const string& runName, const MSData& msd)
{
    TableWriterPtr chroms = connection.tableWriter("chroms");

    if (msd.run.chromatogramListPtr)
    {
        const ChromatogramList& cl = *msd.run.chromatogramListPtr;
        for (size_t i = 0, end = cl.size(); i < end; ++i)
        {
            ChromatogramPtr c = cl.chromatogram(i, true);
            BinaryDataArrayPtr timeArray = c->getTimeArray();
            BinaryDataArrayPtr intensityArray = c->getIntensityArray();
            if (!timeArray || !intensityArray)
                continue;

            // times are written in minutes; if the time units are unknown, rt is NULL
            double minutesPerUnit = minutesPerTimeUnit(timeArray->cvParam(MS_time_array).units);
            boost::optional<double> targetMz = optionalValue<double>(c->precursor.isolationWindow.cvParam(MS_isolation_window_target_m_z));
            boost::optional<double> productMz = optionalValue<double>(c->product.isolationWindow.cvParam(MS_isolation_window_target_m_z));

            for (size_t p = 0, pointCount = min(timeArray->data.size(), intensityArray->data.size()); p < pointCount; ++p)
            {
                boost::optional<double> rt;
                if (minutesPerUnit > 0)
                    rt = timeArray->data[p] * minutesPerUnit;

                chroms->append(runName); chroms->append(c->id); chroms->append((int) c->index); chroms->append(targetMz);
                chroms->append(productMz); chroms->append(rt); chroms->append(intensityArray->data[p]);
                chroms->endRow();
            }
        }
    }

    chroms->close();
}


/// writes the rows of every table for one run
void writeRows(Connection& connection, const string& runName, const MSData& msd, const MSDataFile::WriteConfig& config,
               const string& serializerName, const IterationListenerRegistry* iterationListenerRegistry)
{
    TableWriterPtr ms1 = connection.tableWriter("MS1");
    TableWriterPtr ms2 = connection.tableWriter("MS2");
    TableWriterPtr scanInfo = connection.tableWriter("scan_info");

    int scansWritten = 0;
    boost::optional<double> rtStart, rtEnd;
    string ionMobilityTypeName; // of the first ion mobility value or array seen in the run

    SpectrumList& sl = *msd.run.spectrumListPtr;
    SpectrumWorkerThreads spectrumWorkers(sl, config.useWorkerThreads, config.continueOnError);
    for (size_t i = 0, end = sl.size(); i < end; ++i)
    {
        SpectrumPtr s;
        try
        {
            s = spectrumWorkers.processBatch(i);
        }
        catch (std::exception& e)
        {
            if (config.continueOnError)
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

        // a multiplexed spectrum has several precursors; its MS2 rows are repeated for each of them
        vector<PrecursorInfo> precursors;
        if (msLevel && *msLevel > 1)
        {
            for (const Precursor& precursor : s->precursors)
                precursors.push_back(precursorInfo(precursor));
        }
        if (precursors.empty())
            precursors.push_back(PrecursorInfo());

        // ion mobility is either a single value for the spectrum or an array with a value per point
        CVParam ionMobilityParam = spectrumIonMobility(*s);
        boost::optional<double> spectrumIonMobilityValue = optionalValue<double>(ionMobilityParam);
        BinaryDataArrayPtr ionMobilityArray = s->getArrayByCVID(MS_ion_mobility_array, true);
        if (ionMobilityTypeName.empty() && ionMobilityArray)
            ionMobilityTypeName = ionMobilityType(ionMobilityArray->cvParamChild(MS_ion_mobility_array));
        else if (ionMobilityTypeName.empty() && spectrumIonMobilityValue)
            ionMobilityTypeName = ionMobilityType(ionMobilityParam);

        // the m/z array specifically (getMZArray() also returns the wavelength array of UV spectra)
        BinaryDataArrayPtr mzArray = s->getArrayByCVID(MS_m_z_array);
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

            boost::optional<double> ionMobility = spectrumIonMobilityValue;
            if (ionMobilityArray && p < ionMobilityArray->data.size())
                ionMobility = ionMobilityArray->data[p];

            if (msLevel && *msLevel == 1)
            {
                ms1->append(runName); ms1->append(scanIndex); ms1->append(rt);
                ms1->append(mz); ms1->append(intensity); ms1->append(ionMobility);
                ms1->endRow();
            }
            else if (msLevel && *msLevel == 2)
            {
                for (const PrecursorInfo& precursor : precursors)
                {
                    ms2->append(runName); ms2->append(scanIndex); ms2->append(rt); ms2->append(precursor.mz);
                    ms2->append(mz); ms2->append(intensity); ms2->append(precursor.voltage); ms2->append(ionMobility);
                    ms2->endRow();
                }
            }
        }

        scanInfo->append(runName); scanInfo->append(scanIndex); scanInfo->append(s->id); scanInfo->append(msLevel);
        scanInfo->append(rt); scanInfo->append(polarity); scanInfo->append(centroided); scanInfo->append(precursors[0].mz);
        scanInfo->append(precursors[0].voltage); scanInfo->append(tic); scanInfo->append(bpc); scanInfo->append(minMz);
        scanInfo->append(maxMz); scanInfo->append(spectrumIonMobilityValue);
        scanInfo->endRow();
        ++scansWritten;

        // update any listeners and handle cancellation
        if (iterationListenerRegistry &&
            iterationListenerRegistry->broadcastUpdateMessage(IterationListener::UpdateMessage(i, end)) == IterationListener::Status_Cancel)
            throw runtime_error("[" + serializerName + "::write()] conversion cancelled");
    }

    ms1->close();
    ms2->close();
    scanInfo->close();

    writeChromatograms(connection, runName, msd);

    TableWriterPtr fileInfo = connection.tableWriter("file_info");
    fileInfo->append(runName);
    fileInfo->append(scansWritten);
    fileInfo->append(rtStart);
    fileInfo->append(rtEnd);
    fileInfo->appendOrNull(instrumentModel(msd));
    fileInfo->appendOrNull(msd.run.startTimeStamp);
    fileInfo->append(pwiz::Version::str());
    fileInfo->appendOrNull(commandLineArgs(msd));
    fileInfo->appendOrNull(ionMobilityTypeName);
    fileInfo->endRow();
    fileInfo->close();
}


} // namespace


void writeRun(Connection& connection, const string& databaseFilename, const MSData& msd,
              const MSDataFile::WriteConfig& config, const string& serializerName,
              const IterationListenerRegistry* iterationListenerRegistry)
{
    string runName = config.inputFilename.empty() ? msd.run.id : config.inputFilename;
    if (runName.empty())
        throw runtime_error("[" + serializerName + "::write()] no input filename or run id to identify the run by");

    connection.execute(createTablesSql);

    // the whole run is written in one transaction, so a failed conversion leaves the database as it was
    connection.execute("BEGIN TRANSACTION");
    try
    {
        if (connection.executeWithFilename("SELECT count(*) FROM file_info WHERE filename = ?", runName) > 0)
        {
            if (!config.replaceExistingDatabaseRuns)
                throw user_error("[" + serializerName + "::write()] \"" + runName + "\" is already in " + databaseFilename +
                                 "; use --replaceExistingDatabaseRuns to replace it");

            for (const char* table : tableNames)
                connection.executeWithFilename(string("DELETE FROM ") + table + " WHERE filename = ?", runName);
        }

        // the table writers are destroyed when writeRows returns or throws, before the transaction ends
        writeRows(connection, runName, msd, config, serializerName, iterationListenerRegistry);
        connection.execute("COMMIT");
    }
    catch (...)
    {
        try
        {
            connection.execute("ROLLBACK");
        }
        catch (...)
        {
            // report the original error, not a failure to roll back
        }
        throw;
    }
}


} // namespace database
} // namespace msdata
} // namespace pwiz
