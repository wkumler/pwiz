﻿/*
 * Original author: Brendan MacLean <brendanx .at. u.washington.edu>,
 *                  MacCoss Lab, Department of Genome Sciences, UW
 *
 * Copyright 2009 University of Washington - Seattle, WA
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using pwiz.BiblioSpec;
using pwiz.Common.SystemUtil;
using pwiz.Skyline.Model.Irt;
using pwiz.Skyline.Model.Results;
using pwiz.Skyline.Properties;
using pwiz.Skyline.Util;
using pwiz.Skyline.Util.Extensions;

namespace pwiz.Skyline.Model.Lib
{
    public interface IiRTCapableLibraryBuilder : ILibraryBuilder
    {
        string AmbiguousMatchesMessage { get; }
        IrtStandard IrtStandard { get; }
        string BuildCommandArgs { get; }
        string BuildOutput { get; }
    }

    public sealed class BiblioSpecLiteBuilder : IiRTCapableLibraryBuilder
    {
        // ReSharper disable LocalizableElement
        public const string EXT_PEP_XML = ".pep.xml";
        public const string EXT_PEP_XML_ONE_DOT = ".pepXML";
        public const string EXT_MZID = ".mzid";
        public const string EXT_MZID_GZ = ".mzid.gz";
        public const string EXT_IDP_XML = ".idpXML";
        public const string EXT_SQT = ".sqt";
        public const string EXT_DAT = ".dat";
        public const string EXT_SSL = ".ssl";
        public const string EXT_XTAN_XML = ".xtan.xml";
        public const string EXT_PROTEOME_DISC = ".msf";
        public const string EXT_PROTEOME_DISC_FILTERED = ".pdResult";
        public const string EXT_PILOT = ".group";
        public const string EXT_PILOT_XML = ".group.xml";
        public const string EXT_PRIDE_XML = ".pride.xml";
        public const string EXT_PERCOLATOR = ".perc.xml";
        public const string EXT_PERCOLATOR_XML = "results.xml";
        public const string EXT_MAX_QUANT = "msms.txt";
        public const string EXT_WATERS_MSE = "final_fragment.csv";
        public const string EXT_PROXL_XML = ".proxl.xml";
        public const string EXT_TSV = ".tsv";
        public const string EXT_MZTAB = ".mzTab";
        public const string EXT_MZTAB_TXT = "mztab.txt";
        public const string EXT_OPEN_SWATH = ".osw";
        public const string EXT_SPECLIB = ".speclib";
        // ReSharper restore LocalizableElement

        private ReadOnlyCollection<string> _inputFiles;

        public BiblioSpecLiteBuilder(string name, string outputPath, IList<string> inputFiles, IList<Target> targetSequences = null, bool useExplicitPeakBounds = true)
        {
            LibrarySpec = new BiblioSpecLiteSpec(name, outputPath, useExplicitPeakBounds);

            InputFiles = inputFiles;

            // FUTURE(bspratt) small molecule workflows
            if (targetSequences != null)
                TargetSequences = targetSequences.Where(t => t.IsProteomic).Select(t => t.Sequence).ToList();
        }

        public LibrarySpec LibrarySpec { get; private set; }
        public string OutputPath { get { return LibrarySpec.FilePath; } }

        public LibraryBuildAction Action { get; set; }
        public bool KeepRedundant { get; set; }
        public double? CutOffScore { get; set; }
        public Dictionary<string, double> ScoreThresholdsByFile { get; set; }
        public string Id { get; set; }
        public bool IncludeAmbiguousMatches { get; set; }
        public IrtStandard IrtStandard { get; set; }
        public bool? PreferEmbeddedSpectra { get; set; }
        public IList<int> Charges { get; set; } // Optional list of desired charges, if populated BlibBuild ignores any not listed
        public bool DebugMode { get; set; }

        public IList<string> InputFiles
        {
            get { return _inputFiles; }
            private set { _inputFiles = value as ReadOnlyCollection<string> ?? new ReadOnlyCollection<string>(value); }
        }

        public IList<string> TargetSequences { get; private set; }

        public string AmbiguousMatchesMessage
        {
            get
            {
                return _ambiguousMatches != null && _ambiguousMatches.Any()
                    ? TextUtil.LineSeparate(
                        Resources.BiblioSpecLiteBuilder_AmbiguousMatches_The_library_built_successfully__Spectra_matching_the_following_peptides_had_multiple_ambiguous_peptide_matches_and_were_excluded_,
                        string.Join(Environment.NewLine, _ambiguousMatches))
                    : string.Empty;
            }
        }

        public string BuildCommandArgs { get { return _buildCommandArgs; } }
        public string BuildOutput { get { return _buildOutput; } }

        private string[] _ambiguousMatches;
        private string _buildCommandArgs;
        private string _buildOutput;

        public bool BuildLibrary(IProgressMonitor progress)
        {
            _ambiguousMatches = null;
            IProgressStatus status = new ProgressStatus(LibResources.BiblioSpecLiteBuilder_BuildLibrary_Preparing_to_build_library);
            progress.UpdateProgress(status);
            if (InputFiles.Any(f => f.EndsWith(EXT_PILOT)))
            {
                try
                {
                    InputFiles = VendorIssueHelper.ConvertPilotFiles(InputFiles, progress, status);
                    if (progress.IsCanceled)
                        return false;
                }
                catch (Exception x)
                {
                    progress.UpdateProgress(status.ChangeErrorException(x));
                    return false;
                }
            }

            string message = string.Format(LibResources.BiblioSpecLiteBuilder_BuildLibrary_Building__0__library,
                                           Path.GetFileName(OutputPath));
            progress.UpdateProgress(status = status.ChangeMessage(message));
            string redundantLibrary = BiblioSpecLiteSpec.GetRedundantName(OutputPath);
            var blibBuilder = new BlibBuild(redundantLibrary, InputFiles, TargetSequences)
            {
                IncludeAmbiguousMatches = IncludeAmbiguousMatches,
                CutOffScore = CutOffScore,
                ScoreThresholdsByFile = ScoreThresholdsByFile,
                Id = Id,
                PreferEmbeddedSpectra = PreferEmbeddedSpectra,
                DebugMode = DebugMode,
                Charges = Charges,
            };

            try
            {
                if (!blibBuilder.BuildLibrary(Action, progress, ref status,
                    out _buildCommandArgs, out _buildOutput, out _ambiguousMatches))
                {
                    return false;
                }
            }
            catch (IOException x)
            {
                if (IsLibraryMissingExternalSpectraError(x, out IList<string> spectrumFilenames, out IList<string> directoriesSearched, out string resultsFilepath))
                {
                    // replace the relative path to the results file (e.g. msms.txt) with the absolute path
                    string fullResultsFilepath = InputFiles.SingleOrDefault(o => o.EndsWith(resultsFilepath));
                    if (fullResultsFilepath == null)
                        throw new InvalidDataException(@"no results filepath from BiblioSpec error message", x);

                    // TODO: this will break if BiblioSpec output is translated to other languages
                    string messageWithFullFilepath =
                        x.Message.Replace(@"search results file '" + resultsFilepath,
                                          @"search results file '" + fullResultsFilepath);
                    x = new IOException(messageWithFullFilepath, x);
                }

                progress.UpdateProgress(status.ChangeErrorException(x));
                return false;
            }
            catch (Exception x)
            {
                Console.WriteLine(x.Message);
                progress.UpdateProgress(status.ChangeErrorException(
                    new Exception(string.Format(LibResources.BiblioSpecLiteBuilder_BuildLibrary_Failed_trying_to_build_the_redundant_library__0__,
                                                redundantLibrary), x)));
                return false;
            }

            status = new ProgressStatus(message);
            progress.UpdateProgress(status);
            try
            {
                if (BiblioSpecLiteLibrary.IsRedundantLibrary(redundantLibrary))
                {
                    var blibFilter = new BlibFilter();
                    // Write the non-redundant library to a temporary file first
                    using (var saver = new FileSaver(OutputPath))
                    {
                        if (!blibFilter.Filter(redundantLibrary, saver.SafeName, progress, ref status))
                        {
                            return false;
                        }

                        saver.Commit();
                    }
                }
                else // rename as non-redundant
                {
                    File.Move(redundantLibrary, OutputPath!);
                }
            }
            catch (IOException x)
            {
                progress.UpdateProgress(status.ChangeErrorException(x));
                return false;
            }
            catch (Exception x)
            {
                progress.UpdateProgress(status.ChangeErrorException(
                    new Exception(string.Format(LibResources.BiblioSpecLiteBuilder_BuildLibrary_Failed_trying_to_build_the_library__0__,
                                                OutputPath), x)));
                return false;
            }
            finally
            {
                if (!KeepRedundant)
                    FileEx.SafeDelete(redundantLibrary, true);
            }

            return true;
        }

        public static bool HasEmbeddedSpectra(string libraryInputFilepath)
        {
            return libraryInputFilepath.EndsWith(EXT_MAX_QUANT);
        }

        public static bool IsLibraryMissingExternalSpectraError(Exception errorException)
        {
            // ReSharper disable UnusedVariable
            return IsLibraryMissingExternalSpectraError(errorException, out IList<string> s1, out IList<string> s2, out string s3);
            // ReSharper restore UnusedVariable
        }

        public static bool IsLibraryMissingExternalSpectraError(Exception errorException, out IList<string> spectrumFilenames, out IList<string> directoriesSearched, out string resultsFilepath)
        {
            spectrumFilenames = null;
            directoriesSearched = null;
            resultsFilepath = null;

            // TODO: this test (and the regex below) will break if BiblioSpec output is translated to other languages
            if (!errorException.Message.Contains(@"Run with the -E flag"))
                return false;
            // ReSharper disable once LocalizableElement
            string rawMessage = errorException.Message.Replace(@"ERROR: ", "");
            var messageParts = Regex.Match(rawMessage, @"While searching .* results file '([^']+)'.*$\n(?:(.+)\n)+In any of the following directories\:(?:(.+)\n)+Run with the -E flag", RegexOptions.Multiline);
            if (!messageParts.Success)
                throw new InvalidDataException(@"failed to parse filenames from BiblioSpec error message", errorException);

            spectrumFilenames = new List<string>();
            foreach (Capture line in messageParts.Groups[2].Captures)
            {
                var lineTrimmed = line.Value.Trim();
                if (lineTrimmed.Length > 0)
                    spectrumFilenames.Add(lineTrimmed);
            }

            directoriesSearched = new List<string>();
            foreach (Capture line in messageParts.Groups[3].Captures)
            {
                var lineTrimmed = line.Value.Trim();
                if (lineTrimmed.Length > 0)
                    directoriesSearched.Add(lineTrimmed);
            }

            resultsFilepath = messageParts.Groups[1].Value;

            return HasEmbeddedSpectra(resultsFilepath);
        }

        public static string BiblioSpecSupportedFileExtensions => @"mz5, mzML, raw, wiff, d, lcd, mzXML, cms2, ms2, or mgf";

        public static double? GetDefaultScoreThreshold(string scoreName, double? defaultThreshold = null)
        {
            foreach (var s in Settings.Default.BlibLibraryThresholds ?? new StringCollection())
            {
                var parsed = ParseScoreThresholdSetting(s);
                if (parsed != null && Equals(parsed.Item1, scoreName))
                    return parsed.Item2;
            }
            return defaultThreshold;
        }

        public static void SetDefaultScoreThreshold(string scoreName, double? threshold)
        {
            for (var i = 0; i < Settings.Default.BlibLibraryThresholds?.Count; i++)
            {
                var parsed = ParseScoreThresholdSetting(Settings.Default.BlibLibraryThresholds[i]);
                if (Equals(parsed.Item1, scoreName))
                {
                    if (threshold.HasValue)
                        Settings.Default.BlibLibraryThresholds[i] = scoreName + @"=" + threshold.Value.ToString(CultureInfo.InvariantCulture);
                    else
                        Settings.Default.BlibLibraryThresholds.RemoveAt(i);
                    return;
                }
            }
            if (Settings.Default.BlibLibraryThresholds == null)
                Settings.Default.BlibLibraryThresholds = new StringCollection();
            if (threshold.HasValue)
                Settings.Default.BlibLibraryThresholds.Add(scoreName + @"=" + threshold.Value.ToString(CultureInfo.InvariantCulture));
        }

        private static Tuple<string, double> ParseScoreThresholdSetting(string s)
        {
            var i = s.IndexOf('=');
            return i >= 0 && double.TryParse(s.Substring(i + 1), NumberStyles.Float, CultureInfo.InvariantCulture, out var threshold)
                ? Tuple.Create(s.Substring(0, i), threshold)
                : null;
        }
    }
}
