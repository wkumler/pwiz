//
// $Id$
//
//
// Original author: Barbara Frewen <frewen@u.washington.edu>
//
// Copyright 2012 University of Washington - Seattle, WA 98195
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

/**
 * Class for building a library from search result files and their
 * accomanying spectrum files and/or from existing libraries.  Extends
 * BlibMaker.
 *
 * $ BlibBuilder.cpp,v 1.0 2009/01/07 15:53:52 Ning Zhang Exp $
 */

#include "BlibBuilder.h"
#include "SqliteRoutine.h"
#include "pwiz/utility/misc/Filesystem.hpp"
#include "boost/filesystem/detail/utf8_codecvt_facet.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/join.hpp>


namespace BiblioSpec {

BlibBuilder::BlibBuilder():
level_compress(3), fileSizeThresholdForCaching(800000000),
targetSequences(NULL), targetSequencesModified(NULL), stdinStream(&cin),
forcedPusherInterval(-1), explicitCutoff(-1)
{
}

BlibBuilder::~BlibBuilder()
{
    if (stdinStream != &cin && stdinStream != NULL)
    {
        ((ifstream*)stdinStream)->close();
        delete stdinStream;
        stdinStream = NULL;
    }
    if (targetSequences != NULL)
    {
        delete targetSequences;
        targetSequences = NULL;
    }
    if (targetSequencesModified != NULL)
    {
        delete targetSequencesModified;
        targetSequencesModified = NULL;
    }
}

static vector<string> supportedTypes = {
    ".blib",
    ".pep.xml",
    ".pep.XML",
    ".pepXML",
    ".sqt",
    ".perc.xml",
    ".dat",
    ".xtan.xml",
    ".idpXML",
    ".group.xml",
    ".group", // getting score type only
    ".pride.xml",
    ".msf",
    ".pdResult",
    ".mzid",
    ".mzid.gz",
    "msms.txt",
    "final_fragment.csv",
    ".proxl.xml",
    ".ssl",
    ".hk.bs.kro", // Hardklor result file postprocessed by BullseyeSharp
    ".mlb",
    ".speclib",
    ".tsv",
    ".osw",
    ".mzTab",
    "mztab.txt"
};

void BlibBuilder::usage()
{
    std::string usage =
        "Usage: BlibBuild [options] <*" + boost::algorithm::join(supportedTypes, "|*") + ">+ <library_name>\n"
        "   -o                Overwrite existing library. Default append.\n"
        "   -S  <filename>    Read from file as though it were stdin.\n"
        "   -s                Result file names from stdin. e.g. ls *sqt | BlibBuild -s new.blib.\n"
        "   -u                Ignore peptides except those with the unmodified sequences from stdin.\n"
        "   -U                Ignore peptides except those with the modified sequences from stdin.\n"
        "   -H                Use more than one decimal place when describing mass modifications.\n"
        "   -C  <file size>   Minimum file size required to use caching for .dat files.  Specifiy units as B,K,G or M.  Default 800M.\n"
        "   -c <cutoff>       Score threshold (0-1) for PSMs to be included in library. Higher threshold is more exclusive.\n"
        "   -v  <level>       Level of output to stderr (silent, error, status, warn).  Default warn.\n"
        "   -T                Add prefixes to log output showing time elapsed.\n"
        "   -L                Write status and warning messages to log file.\n"
        "   -m <size>         SQLite memory cache size in Megs. Default 250M.\n"
        "   -l <level>        ZLib compression level (0-?). Default 3.\n"
        "   -i <library_id>   LSID library ID. Default uses file name.\n"
        "   -a <authority>    LSID authority. Default proteome.gs.washington.edu.\n"
        "   -x <filename>     Specify the path of XML modifications file for parsing MaxQuant files.\n"
        "   -p <filename>     Specify the path of XML parameters file for parsing MaxQuant files.\n"
        "   -P <float>        Specify pusher interval for Waters final_fragment.csv files.\n"
        "   -d [<filename>]   Document the .blib format by writing SQLite commands to a file, or stdout if no filename is given.\n"
        "   -E                Prefer reading peaks from embedded spectra (currently only affects MaxQuant msms.txt)\n"
        "   -A                Output messages noting ambiguously matched spectra (spectra matched to multiple peptides)\n"
        "   -K                Keep ambiguously matched spectra\n"
        "   -t                Only output score types (no library build).\n"
        "   -z <charges>      Only output PSMs with these charges, e.g. \"2,3\".\n";

    cerr << usage << endl;
    exit(1);
}

double BlibBuilder::getScoreThreshold(BUILD_INPUT fileType) {
    map<string, double>::const_iterator i = inputThresholds.find(input_files[curFile]);
    if (i != inputThresholds.end()) {
        return i->second;
    }
    switch (fileType) {
    case SQT: // FDR
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.01;
    case PEPXML: // peptide prophet probability
        return explicitCutoff >= 0 ? explicitCutoff : 0.95;
    case IDPXML:
        return 0; // use all results
    case MASCOT: // expectation value
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.05;
    case TANDEM: // expect score
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.1;
    case PROT_PILOT: // confidence
        return explicitCutoff >= 0 ? explicitCutoff : 0.95;
    case SCAFFOLD: // peptide probability
        return explicitCutoff >= 0 ? explicitCutoff : 0.95;
    case MSE: // Waters MSe peptide score
        return 6;
    case OMSSA: // max OMSSA expect score
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.00001;
    case PROT_PROSPECT: // expect score
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.001;
    case MAXQUANT: // PEP
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.05;
    case MORPHEUS: // PSM q-value
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.01;
    case MSGF: // PSM q-value
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.01;
    case PEAKS: // p-value
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.05;
    case BYONIC: // PEP
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.05;
    case PEPTIDE_SHAKER: // PSM confidence
        return explicitCutoff >= 0 ? explicitCutoff : 0.95;
    case GENERIC_QVALUE_INPUT:
        return explicitCutoff >= 0 ? 1 - explicitCutoff : 0.01;
    }
    return -1;
}

int BlibBuilder::getLevelCompress() {
    return level_compress;
}

vector<string> BlibBuilder::getInputFiles() {
    return input_files;
}

void BlibBuilder::setCurFile(int i) {
    curFile = i;
}

int BlibBuilder::getCurFile() const {
    return curFile;
}

string BlibBuilder::getMaxQuantModsPath() {
    return maxQuantModsPath;
}

string BlibBuilder::getMaxQuantParamsPath() {
    return maxQuantParamsPath;
}

double BlibBuilder::getPusherInterval() const {
    return forcedPusherInterval;
}

const set<string>* BlibBuilder::getTargetSequences() {
    return targetSequences;
}

const set<string>* BlibBuilder::getTargetSequencesModified() {
    return targetSequencesModified;
}

/**
 * Read the command line.  Use BlibMaker to parse options.  Get
 * filenames and store in input_files vector.
 * \returns The number of ??
 */
int BlibBuilder::parseCommandArgs(int argc, char* argv[])
{
    int i = BlibMaker::parseCommandArgs(argc, argv);
    if (!isScoreLookupMode()) {
        argc--; // Remove output library at the end
    }

    bool filesFromStdin = false;
    while (!stdinput.empty()) {
        // handle list
        switch (stdinput.front()) {
        case FILENAMES:
            // read filenames until end of cin or empty line
            filesFromStdin = true;
            Verbosity::debug("Reading input filenames");
            while (*stdinStream) {
                string infileName;
                getline(*stdinStream, infileName);
                bal::trim(infileName);
                if (infileName.empty()) {
                    break;
                }
                const string thresholdSearch = "score_threshold=";
                size_t thresholdIdx = infileName.rfind(thresholdSearch);
                if (thresholdIdx == string::npos) {
                    Verbosity::debug("Input file: %s", infileName.c_str());
                } else {
                    string thresholdStr = infileName.substr(thresholdIdx + thresholdSearch.length());
                    infileName = infileName.substr(0, thresholdIdx);
                    bal::trim(infileName);
                    double threshold = stod(thresholdStr);
                    Verbosity::debug("Input file: %s (cutoff = %.2f)", infileName.c_str(), threshold);
                    inputThresholds[infileName] = threshold;
                }
                input_files.push_back(infileName);
            }
            break;
        case UNMODIFIED_SEQUENCES:
            // read unmodified sequences until end of cin or empty line
            Verbosity::debug("Reading target unmodified sequences");
            readSequences(&targetSequences);
            break;
        case MODIFIED_SEQUENCES:
            // read modified sequences until end of cin or empty line
            Verbosity::debug("Reading target modified sequences");
            readSequences(&targetSequencesModified, true);
            break;
        }

        stdinput.pop();
    }

    if (stdinStream != &cin && stdinStream != NULL)
    {
        ((ifstream*)stdinStream)->close();
    }

    if (!filesFromStdin) {
        int nInputs = argc - i;
        if (nInputs < 1)
        {
            Verbosity::comment(V_ERROR,
                "Not enough arguments. Missing input files (%s.), or no output file specified.", boost::algorithm::join(supportedTypes, ", ").c_str());
            usage();          // Nothing to add
        }
        else
        {
            for (int j = i; j < argc; j++) {
                char* file_name = argv[j];
                //if (has_extension(file_name, ".blib"))
                //merge_libs[merge_count++] = file_name;
                bool supported = false;
                for (vector<string>::const_iterator ext = supportedTypes.begin(); !supported && ext != supportedTypes.end(); ++ext)
                {
                    supported = has_extension(file_name, ext->c_str());
                }
                if (supported) {
                    input_files.push_back(string(file_name));
                } else {
                    Verbosity::error("Unsupported file type '%s'.  Must be one of %s.",
                        file_name, boost::algorithm::join(supportedTypes, ", ").c_str());
                }
            }
        }
    }

    return i;
}


void BlibBuilder::attachAll()
{
    // we are no longer attaching here, do it one file at a time
    /*
    for (int i = 0; i < (int)input_files.size(); i++) {
        if(has_extension(input_files.at(i), ".blib")) {
            sprintf(zSql, "ATTACH DATABASE '%s' as tmp%d", input_files.at(i), i);
            sql_stmt(zSql);
        }
    }
    */
}

int BlibBuilder::transferLibrary(int iLib,
                                 const ProgressIndicator* parentProgress)
{
    // Check to see if library exists
    verifyFileExists(input_files.at(iLib));

    // give the incomming library a name
    char schemaTmp[32];
    sprintf(schemaTmp, "tmp%d", iLib);

    // add it to our open libraries
    sprintf(zSql, "ATTACH DATABASE '%s' as %s", input_files.at(iLib).c_str(), schemaTmp);
    sql_stmt(zSql);

    createUpdatedRefSpectraView(schemaTmp);

    // does the incomming library have retentiontime, score, etc columns
    int tableVersion = 0;
    if (tableColumnExists(schemaTmp, "RefSpectra", "retentionTime")) {
        if (tableColumnExists(schemaTmp, "RefSpectra", "startTime")) {
            tableVersion = MIN_VERSION_TIC;
        } else if (tableColumnExists(schemaTmp, "RefSpectra", "startTime")) {
            tableVersion = MIN_VERSION_RT_BOUNDS;
        } else if (tableExists(schemaTmp, "RefSpectraPeakAnnotations")) {
            tableVersion = MIN_VERSION_PEAK_ANNOT;
        } else if (tableColumnExists(schemaTmp, "RefSpectra", "ionMobilityHighEnergyOffset")) {
            tableVersion = MIN_VERSION_IMS_UNITS;
        } else if (tableColumnExists(schemaTmp, "RefSpectra", "moleculeName")) {
            tableVersion = MIN_VERSION_SMALL_MOL;
        } else if (tableColumnExists(schemaTmp, "RefSpectra", "collisionalCrossSectionSqA")) {
            tableVersion = MIN_VERSION_CCS;
        } else if (tableColumnExists(schemaTmp, "RefSpectra", "ionMobilityHighEnergyDriftTimeOffsetMsec")) { // As in Waters MsE IMS
            tableVersion = MIN_VERSION_IMS_HEOFF;
        } else if (tableColumnExists(schemaTmp, "RefSpectra", "ionMobilityValue")) {
            tableVersion = MIN_VERSION_IMS;
        } else {
            tableVersion = 1;
        }
    }

    beginTransaction();

    string msg = "ERROR: Failed transfering spectra from ";
    msg += input_files.at(iLib);
    setMessage(msg.c_str());

    Verbosity::status("Transferring spectra from %s.",
                      base_name(input_files.at(iLib).c_str()).c_str());

    // first add all the spectrum source files from incomming library
    transferSpectrumFiles(schemaTmp);

    ProgressIndicator* progress =
        parentProgress->newNestedIndicator(getSpectrumCount(schemaTmp));

    sprintf(zSql, "SELECT id, peptideSeq, peptideModSeq FROM %s.RefSpectra ORDER BY id", schemaTmp);

    smart_stmt pStmt;
    int rc = sqlite3_prepare(getDb(), zSql, -1, &pStmt, 0);

    check_rc(rc, zSql);

    rc = sqlite3_step(pStmt);

    while(rc==SQLITE_ROW) {
        progress->increment();

        bool skip = false;
        if (targetSequences != NULL || targetSequencesModified != NULL) {
            // filtering targets
            string seqUnmodified = boost::lexical_cast<string>(sqlite3_column_text(pStmt, 1));
            string seqModified = parseSequence(boost::lexical_cast<string>(sqlite3_column_text(pStmt, 2)), true);
            skip = !(
                    // don't filter if targetSequences is not null and it contains the unmodified sequence
                    (targetSequences != NULL && targetSequences->find(seqUnmodified) != targetSequences->end()) ||
                    // OR targetSequencesModified is not null and it contains the modified sequence
                    (targetSequencesModified != NULL && targetSequencesModified->find(seqModified) != targetSequencesModified->end())
                );
        }

        if (!skip) {
            // Even if you are transfering from a non-redundant library
            // you only get credit for one spectrum in a redundant library
            int spectraId = sqlite3_column_int(pStmt, 0);
            transferSpectrum(schemaTmp, spectraId, 1, tableVersion);
        }

        rc = sqlite3_step(pStmt);
    }

    sql_stmt("DROP VIEW RefSpectraTransfer");

    endTransaction();
    int numberProcessed =  progress->processed();
    delete progress;
    return numberProcessed;
}

void BlibBuilder::collapseSources() {
    // gather all mzXML files from SpectrumSourceFiles table
    sqlite3_stmt* getFilesStmt;
    if (sqlite3_prepare(getDb(), "SELECT id, filename FROM SpectrumSourceFiles", -1, &getFilesStmt, NULL) != SQLITE_OK) {
        return;
    }

    const string reqExt = ".mzXML";
    map<string, int> fileIds;
    while (sqlite3_step(getFilesStmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(getFilesStmt, 0);
        string name = boost::lexical_cast<string>(sqlite3_column_text(getFilesStmt, 1));
        if (name.length() >= reqExt.length() && boost::iequals(name.substr(name.length() - reqExt.length()), reqExt)) {
            fileIds[name] = id;
        }
    }
    sqlite3_finalize(getFilesStmt);

    // look for complete pattern groups
    vector< vector<string> > patternGrps;
    string grp1[] = { "_Q1.", "_Q2.", "_Q3." };
    string grp2[] = { ".ForLibQ1.", ".ForLibQ2.", ".ForLibQ3." };
    patternGrps.push_back(vector<string>(grp1, grp1 + sizeof(grp1)/sizeof(string)));
    patternGrps.push_back(vector<string>(grp2, grp2 + sizeof(grp2)/sizeof(string)));

    for (map<string, int>::const_iterator i = fileIds.begin(); i != fileIds.end(); i++) {
        string sourceFile = i->first;
        int sourceId = i->second;
        for (vector< vector<string> >::const_iterator grp = patternGrps.begin(); grp != patternGrps.end(); grp++) {
            // check if the file contains the first pattern in the group
            vector<string>::const_iterator pattern = grp->begin();
            size_t pos = sourceFile.find(*pattern);
            if (pos == string::npos)
                continue;

            // check if there is a file for each other pattern in the group
            vector<int> groupIds;
            for (++pattern; pattern != grp->end(); pattern++) {
                string expected = sourceFile;
                expected.replace(pos, grp->front().length(), *pattern);
                map<string, int>::const_iterator expectedLookup = fileIds.find(expected);
                if (expectedLookup == fileIds.end())
                    break;

                groupIds.push_back(expectedLookup->second);
            }
            if (groupIds.size() != grp->size() - 1)
                continue;

            // found entire pattern group
            stringstream updateBuilder, deleteBuilder;
            updateBuilder << "UPDATE RefSpectra SET fileID = " << sourceId << " WHERE";
            deleteBuilder << "DELETE FROM SpectrumSourceFiles WHERE";
            for (vector<int>::const_iterator groupId = groupIds.begin(); groupId != groupIds.end(); groupId++) {
                if (groupId != groupIds.begin()) {
                    updateBuilder << " OR";
                    deleteBuilder << " OR";
                }
                updateBuilder << " fileID = " << *groupId;
                deleteBuilder << " id = " << *groupId;
            }
            sqlite3_exec(getDb(), updateBuilder.str().c_str(), NULL, NULL, NULL);
            sqlite3_exec(getDb(), deleteBuilder.str().c_str(), NULL, NULL, NULL);

            updateBuilder.str("");
            sourceFile.replace(pos, grp->front().length(), ".");
            updateBuilder << "UPDATE SpectrumSourceFiles SET fileName = '"
                          << SqliteRoutine::ESCAPE_APOSTROPHES(sourceFile) << "'"
                          << " WHERE id = " << sourceId;
            sqlite3_exec(getDb(), updateBuilder.str().c_str(), NULL, NULL, NULL);
            break;
        }
    }
}

void BlibBuilder::commit()
{
    BlibMaker::commit();

    for (int i = 0; i < (int)input_files.size(); i++) {
        if(has_extension(input_files.at(i).c_str(), ".blib")) {
            sprintf(zSql, "DETACH DATABASE tmp%d", i);
            sql_stmt(zSql);
        }
    }
}

int BlibBuilder::parseNextSwitch(int i, int argc, char* argv[])
{
    char* arg = argv[i];
    char switchName = arg[1];

    if (switchName == 'o')
        setOverwrite(true);
    else if(switchName == 's')
        stdinput.push(FILENAMES);
    else if (switchName == 'S' && ++i < argc) {
        stdinStream = new ifstream(argv[i]);
        if (!stdinStream->good())
        {
            Verbosity::error("Could not open file %s as stdin.", argv[i]);
        }
    } else if (switchName == 'c' && ++i < argc) {
        explicitCutoff = atof(argv[i]);
    } else if (switchName == 'l' && ++i < argc) {
        level_compress = atoi(argv[i]);
    } else if (switchName == 'C' && ++i < argc) {
        int value = atoi(argv[i]);
        // get the last character for units
        const char* token = argv[i];
        char lastChar = *(token + strlen(token) -1);
        if( lastChar == 'B' || lastChar == 'b'){
          fileSizeThresholdForCaching = value;
        } else if( lastChar == 'K' || lastChar == 'k'){
          fileSizeThresholdForCaching = value * 1000;
        } else if( lastChar == 'M' || lastChar == 'm'){
          fileSizeThresholdForCaching = value * 1000000;
        } else if( lastChar == 'G' || lastChar == 'g' ){
          fileSizeThresholdForCaching = value * 1000000000;
        } else {
          Verbosity::error("File sizes must end in B, K, M or G. '%s' is invalid.", token);
        }
    } else if (switchName == 'v' && ++i < argc) {
        V_LEVEL v_level = Verbosity::string_to_level(argv[i]);
        Verbosity::set_verbosity(v_level);
    } else if (switchName == 'T') {
        Verbosity::set_timestamp(true);
    } else if (switchName == 'x' && ++i < argc) {
        maxQuantModsPath = string(argv[i]);
    } else if (switchName == 'p' && ++i < argc) {
        maxQuantParamsPath = string(argv[i]);
    } else if (switchName == 'P' && ++i < argc) {
        forcedPusherInterval = atof(argv[i]);
    } else if (switchName == 'u') {
        stdinput.push(UNMODIFIED_SEQUENCES);
    } else if (switchName == 'U') {
        stdinput.push(MODIFIED_SEQUENCES);
    } else if (switchName == 'L') {
        Verbosity::open_logfile();
    } else if (switchName == 'A') {
        ambiguityMessages_ = true;
    } else if (switchName == 'K') {
        keepAmbiguous_ = true;
    } else if (switchName == 'H') {
        setHighPrecisionModifications(true);
    } else if (switchName == 'E') {
        preferEmbeddedSpectra_ = true;
    } else {
        return BlibMaker::parseNextSwitch(i, argc, argv);
    }

    return min(argc, i + 1);
}

double BlibBuilder::getCutoffScore() const {
    map<string, double>::const_iterator i = inputThresholds.find(input_files[curFile]);
    return i != inputThresholds.end() ? i->second : explicitCutoff;
}

int BlibBuilder::readSequences(set<string>** seqSet, bool modified)
{
    if (*seqSet == NULL)
    {
        *seqSet = new set<string>;
    }

    int sequencesRead = 0;
    while (*stdinStream)
    {
        string sequence;
        getline(*stdinStream, sequence);
        if (sequence.empty())
        {
            break;
        }

        string newSeq = parseSequence(sequence, modified);
        Verbosity::debug("Adding target sequence %s", newSeq.c_str());
        (*seqSet)->insert(newSeq);
        ++sequencesRead;
    }

    return sequencesRead;
}

string BlibBuilder::parseSequence(const string& sequence, bool modified) {
    string newSeq;
    string unexpected;
    vector<SeqMod> mods;
    size_t aaPosition = 0;
    // check that each character is a letter and convert it to uppercase
    for (size_t i = 0; i < sequence.length(); ++i)
    {
        if (isalpha(sequence[i]))
        {
            newSeq += toupper(sequence[i]);
            ++aaPosition;
        }
        else if (modified && sequence[i] == '[')
        {
            // get modification
            size_t endIdx = sequence.find(']', i + 1);
            if (endIdx != string::npos)
            {
                ++i;
                string massStr = sequence.substr(i, endIdx - i);
                try {
                    double deltaMass = boost::lexical_cast<double>(massStr);
                    SeqMod newMod(aaPosition, deltaMass);
                    mods.push_back(newMod);
                } catch (boost::bad_lexical_cast) {
                    Verbosity::warn("Could not read '%s' as a mass in target sequence %s, skipping this "
                                    "modification", massStr.c_str(), sequence.c_str());
                }
                // move iterator to end of modification
                i = endIdx;
            }
            else
            {
                Verbosity::warn("Ignoring opening bracket without closing bracket in target sequence %s",
                                sequence.c_str());
            }
        }
        else
        {
            unexpected += sequence[i];
        }
    }
    if (!unexpected.empty())
    {
        Verbosity::warn("Ignoring unexpected characters %s in target sequence %s",
                        unexpected.c_str(), sequence.c_str());
    }
    if (modified)
    {
        // We use the low precision form of the sequence for this list.
        // This needs to match the logic in BuildParser::filterBySequence
        newSeq = getLowPrecisionModSeq(newSeq.c_str(), mods);
    }
    return newSeq;
}
    
static const char* getModMassFormat(double mass, bool highPrecision) {
    if (!highPrecision) {
        return "[%+.1f]";
    }
    double decimal = (mass - (int) mass);
    int decimalInt = (int) round(decimal * 100000);
    if (decimalInt == (decimalInt / 10000) * 10000) {
        return "[%+.1f]";
    }
    if (decimalInt == (decimalInt / 1000) * 1000) {
        return "[%+.2f]";
    }
    if (decimalInt == (decimalInt /100) * 100) {
        return "[%+.3f]";
    }
    if (decimalInt == (decimalInt /10) * 10) {
        return "[%+.4f]";
    }
    return "[%+.5f]"; // This should match Skyline's pwiz.Skyline.Model.MassModification.MAX_PRECISION_FOR_LIB value (good as of Dec 2019)
}

string BlibBuilder::getModifiedSequenceWithPrecision(const char* unmodSeq,
                                        const vector<SeqMod>& mods,
                                        bool highPrecision) 
{
    string modifiedSeq(unmodSeq);
    char modBuffer[SMALL_BUFFER_SIZE];

    // insert mods from the rear so that the position remains the same
    for(int i = mods.size() - 1; i > -1; i--) {
        if( mods.at(i).deltaMass == 0 ) {
            continue;
        }
        if( mods.at(i).position > (int)modifiedSeq.size() ){
            Verbosity::error("Cannot modify sequence %s, length %d, at "
                             "position %d. ", modifiedSeq.c_str(), 
                             modifiedSeq.size(), mods.back().position);
        }

        double deltaMass = mods.at(i).deltaMass;
        sprintf(modBuffer, getModMassFormat(deltaMass, highPrecision), deltaMass);
        modifiedSeq.insert(mods.at(i).position, modBuffer);
    }

    return modifiedSeq;
}

/**
 * \brief Create a sequence that includes modifications from an
 * unmodified seq and a list of mods.  Assumes that mods are sorted in
 * increasing order by position and that no two entries in the mods
 * vector are to the same position.
 */
string BlibBuilder::generateModifiedSeq(const char* unmodSeq,
                                        const vector<SeqMod>& mods) {
    return getModifiedSequenceWithPrecision(unmodSeq, mods, isHighPrecisionModifications());
}

string base_name(string name)
{
    size_t slash = name.find_last_of("/\\");
    return (slash != string::npos) ? name.substr(slash+1) : name;
}

bool has_extension(string name, string ext)
{
    return bal::iends_with(name, ext);
}

bool BlibBuilder::keepCharge(int z) const
{
    if (precursorCharges_.size() > 0)
    {
        // Ignore items with unwanted charges
        return precursorCharges_.find(z) != precursorCharges_.end();
    }
    return true;
}


/**
 * Call super classe's insertPeaks with our level of compression
 */
void BlibBuilder::insertPeaks(int spectraID,
                              int peaksCount,
                              double* pM,
                              float* pI) {
    BlibMaker::insertPeaks(spectraID, level_compress, peaksCount, pM, pI);

}

} // namespace

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * End:
 */
