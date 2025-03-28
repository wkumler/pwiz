//
// $Id$
//
//
// Original author: Matt Chambers <matt.chambers <a.t> vanderbilt.edu>
//
// Copyright 2016 Vanderbilt University - Nashville, TN 37232
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


#include "SpectrumList_IonMobility.hpp"
#include "pwiz/utility/misc/Std.hpp"

#include "pwiz/data/vendor_readers/Agilent/SpectrumList_Agilent.hpp"
#include "pwiz/data/vendor_readers/Bruker/SpectrumList_Bruker.hpp"
#include "pwiz/data/vendor_readers/Mobilion/SpectrumList_Mobilion.hpp"
#include "pwiz/data/vendor_readers/Waters/SpectrumList_Waters.hpp"
#include "pwiz/data/vendor_readers/Thermo/SpectrumList_Thermo.hpp"
#include "pwiz/data/vendor_readers/UIMF/SpectrumList_UIMF.hpp"


namespace pwiz {
namespace analysis {


using namespace msdata;
using namespace pwiz::util;


PWIZ_API_DECL
SpectrumList_IonMobility::SpectrumList_IonMobility(const msdata::SpectrumListPtr& inner)
:   SpectrumListWrapper(inner), equipment_(SpectrumList_IonMobility::IonMobilityEquipment::None)
{
    units_ = IonMobilityUnits::none;
    has_mzML_combined_ion_mobility_ = false;
    if (dynamic_cast<detail::SpectrumList_Agilent*>(&*innermost()) != NULL)
    {
        equipment_ = IonMobilityEquipment::AgilentDrift;
        units_ = IonMobilityUnits::drift_time_msec;
    }
    else if (dynamic_cast<detail::SpectrumList_Waters*>(&*innermost()) != NULL)
    {
        auto waters = dynamic_cast<detail::SpectrumList_Waters*>(&*innermost());
        bool isSONAR = waters->hasSonarFunctions();
        equipment_ = isSONAR ? IonMobilityEquipment::WatersSonar : IonMobilityEquipment::WatersDrift;
        units_ = isSONAR ? IonMobilityUnits::waters_sonar : IonMobilityUnits::drift_time_msec;
    }
    else if (dynamic_cast<detail::SpectrumList_Bruker*>(&*innermost()) != NULL)
    {
        if ((dynamic_cast<detail::SpectrumList_Bruker*>(&*innermost()))->hasIonMobility())
        {
            equipment_ = IonMobilityEquipment::BrukerTIMS;
            units_ = IonMobilityUnits::inverse_reduced_ion_mobility_Vsec_per_cm2;
        }
    }
    else if (dynamic_cast<detail::SpectrumList_Thermo*>(&*innermost()) != NULL)
    {
        if ((dynamic_cast<detail::SpectrumList_Thermo*>(&*innermost()))->hasIonMobility())
        {
            equipment_ = IonMobilityEquipment::ThermoFAIMS;
            units_ = IonMobilityUnits::compensation_V;
        }
    }
    else if (dynamic_cast<detail::SpectrumList_UIMF*>(&*innermost()) != NULL)
    {
        equipment_ = IonMobilityEquipment::UIMFDrift;
        units_ = IonMobilityUnits::drift_time_msec;
    }
    else if (dynamic_cast<detail::SpectrumList_Mobilion*>(&*innermost()) != NULL)
    {
        equipment_ = IonMobilityEquipment::MobilIonDrift;
        units_ = IonMobilityUnits::drift_time_msec;
    }
    else // reading an mzML conversion?
    {
        if (inner->size() == 0)
            return;

        // See if first few scans have any ion mobility data
        int probeLimit = min(5, (int)inner->size());
        for (int probe = 0; probe < probeLimit; probe++)
        {
            SpectrumPtr spectrum = inner->spectrum(probe, true);
            if (spectrum)
            {
                Scan dummy;
                Scan& scan = spectrum->scanList.scans.empty() ? dummy : spectrum->scanList.scans[0];
                if (scan.hasCVParam(CVID::MS_ion_mobility_drift_time))
                    units_ = IonMobilityUnits::drift_time_msec;
                else if (scan.hasCVParam(CVID::MS_inverse_reduced_ion_mobility))
                    units_ = IonMobilityUnits::inverse_reduced_ion_mobility_Vsec_per_cm2;
                else if (spectrum->hasCVParam(CVID::MS_FAIMS_compensation_voltage))
                    units_ = IonMobilityUnits::compensation_V;
                else if (!scan.userParam("drift time").empty()) // Oldest known mzML drift time style
                    units_ = IonMobilityUnits::drift_time_msec;
                for (const auto& binaryArray : spectrum->binaryDataArrayPtrs)
                {
                    CVID ionMobilityArrayType = binaryArray->cvParamChild(MS_ion_mobility_array).cvid;
                    if (ionMobilityArrayType == CVID_Unknown)
                        continue;

                    if (ionMobilityArrayType == MS_raw_ion_mobility_array || ionMobilityArrayType == MS_mean_ion_mobility_array)
                    {
                        has_mzML_combined_ion_mobility_ = true;
                        units_ = IonMobilityUnits::drift_time_msec;
                        probe = probeLimit; // No need to look further
                        break;
                    }
                    else if (ionMobilityArrayType == MS_raw_inverse_reduced_ion_mobility_array || ionMobilityArrayType == MS_mean_inverse_reduced_ion_mobility_array)
                    {
                        has_mzML_combined_ion_mobility_ = true;
                        units_ = IonMobilityUnits::inverse_reduced_ion_mobility_Vsec_per_cm2;
                        probe = probeLimit; // No need to look further
                        break;
                    }
                }
            }
            else
            {
                spectrum = inner->spectrum(0, true);
                for (const auto& binaryArray : spectrum->binaryDataArrayPtrs)
                {
                    CVID ionMobilityArrayType = binaryArray->cvParamChild(MS_ion_mobility_array).cvid;
                    if (ionMobilityArrayType == CVID_Unknown)
                        continue;

                    if (ionMobilityArrayType == MS_raw_ion_mobility_array || ionMobilityArrayType == MS_mean_ion_mobility_array)
                        units_ = IonMobilityUnits::drift_time_msec;
                    else if (ionMobilityArrayType == MS_raw_inverse_reduced_ion_mobility_array || ionMobilityArrayType == MS_mean_inverse_reduced_ion_mobility_array)
                        units_ = IonMobilityUnits::inverse_reduced_ion_mobility_Vsec_per_cm2;
                    else
                        continue;

                    break;
                }
            }
        }

        sl_ = nullptr;
        return;
    }

    sl_ = dynamic_cast<SpectrumListIonMobilityBase*>(&*innermost());
    if (sl_ == nullptr)
        throw runtime_error("[SpectrumList_IonMobility] BUG: vendor SpectrumList does not inherit from SpectrumListIonMobilityBase");
}

PWIZ_API_DECL SpectrumList_IonMobility::IonMobilityUnits SpectrumList_IonMobility::getIonMobilityUnits() const
{
    return units_;
}

PWIZ_API_DECL bool SpectrumList_IonMobility::accept(const msdata::SpectrumListPtr& inner)
{
    return true; // We'll wrap anything, but getIonMobilityUnits() may well return "none"
}

PWIZ_API_DECL SpectrumPtr SpectrumList_IonMobility::spectrum(size_t index, bool getBinaryData) const
{
    return inner_->spectrum(index, getBinaryData);
}

PWIZ_API_DECL bool SpectrumList_IonMobility::canConvertIonMobilityAndCCS(IonMobilityUnits units) const
{
    if (sl_ == nullptr || units == IonMobilityUnits::none || units != units_)
        return false; // wrong units for this equipment

    return sl_->canConvertIonMobilityAndCCS();
}

PWIZ_API_DECL bool SpectrumList_IonMobility::hasCombinedIonMobility() const
{
    if (sl_ == nullptr)
    {
        // Not a native reader, but mzML may have 3-array representation
        return has_mzML_combined_ion_mobility_;
    }

    return sl_->hasCombinedIonMobility();
}

PWIZ_API_DECL double SpectrumList_IonMobility::ionMobilityToCCS(double ionMobility, double mz, int charge) const
{
    switch (equipment_)
    {
        default:
            throw runtime_error("SpectrumList_IonMobility::ionMobilityToCCS] function only supported when reading native Agilent, Bruker, Mobilion, or Waters files with ion mobility data");

        case IonMobilityEquipment::AgilentDrift:
        case IonMobilityEquipment::BrukerTIMS:
        case IonMobilityEquipment::WatersDrift:
        case IonMobilityEquipment::UIMFDrift:
        case IonMobilityEquipment::MobilIonDrift:
            return sl_->ionMobilityToCCS(ionMobility, mz, charge);
    }
}


PWIZ_API_DECL double SpectrumList_IonMobility::ccsToIonMobility(double ccs, double mz, int charge) const
{
    switch (equipment_)
    {
        default:
            throw runtime_error("SpectrumList_IonMobility::ccsToIonMobility] function only supported when reading native Agilent, Bruker, Mobilion, or Waters files with ion mobility data");

        case IonMobilityEquipment::AgilentDrift:
        case IonMobilityEquipment::BrukerTIMS:
        case IonMobilityEquipment::WatersDrift:
        case IonMobilityEquipment::UIMFDrift:
        case IonMobilityEquipment::MobilIonDrift:
            return sl_->ccsToIonMobility(ccs, mz, charge);
    }
}

///
/// Deal with Waters SONAR equipment, which filters an m/z range using its ion mobility hardware and reports the data as if it were ion mobility
///
PWIZ_API_DECL bool SpectrumList_IonMobility::isWatersSonarData() const
{
    return equipment_ == IonMobilityEquipment::WatersSonar;
}

PWIZ_API_DECL std::pair<int, int> SpectrumList_IonMobility::sonarMzToBinRange(double precursorMz, double tolerance) const
{
    if (!isWatersSonarData())
        throw runtime_error("SpectrumList_IonMobility::sonarMzToBinRange] function only works on Waters SONAR data");

    auto waters = dynamic_cast<detail::SpectrumList_Waters*>(&*innermost());
    return waters->sonarMzToBinRange(precursorMz, tolerance);
}

PWIZ_API_DECL double SpectrumList_IonMobility::sonarBinToPrecursorMz(int bin) const
{
    if (!isWatersSonarData())
        throw runtime_error("SpectrumList_IonMobility::sonarBinToPrecursorMz] function only works on Waters SONAR data");

    auto waters = dynamic_cast<detail::SpectrumList_Waters*>(&*innermost());
    return waters->sonarBinToPrecursorMz(bin);
}

} // namespace analysis 
} // namespace pwiz
