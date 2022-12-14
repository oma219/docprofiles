/*
 * File: pfp_doc.cpp
 * Description: Main file for building the document array profiles
 *              based on the prefix-free parse. This workflow is 
 *              based on the pfp_lcp.hpp developed by Massimiliano
 *              Rossi.
 *
 * Date: August 30th, 2022
 */

#include <pfp_doc.hpp> 
#include <iostream>
#include <fstream>
#include <cstring>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <filesystem>
#include <ref_builder.hpp>
#include <vector>
#include <pfp.hpp>
#include <pfp_lcp_doc.hpp>
#include <doc_queries.hpp>
#include <immintrin.h>

int build_main(int argc, char** argv) {
    /* main method for build the document profiles */
    if (argc == 1) return pfpdoc_build_usage();

    // grab the command-line options, and validate them
    PFPDocBuildOptions build_opts;
    parse_build_options(argc, argv, &build_opts);
    build_opts.validate();

    // determine output path for reference, and print all info
    build_opts.output_ref.assign(build_opts.output_prefix + ".fna");
    print_build_status_info(&build_opts);

    // Build the input reference file, and bitvector labeling the end for each doc
    STATUS_LOG("build_main", "building the reference file based on file-list");
    auto start = std::chrono::system_clock::now();
    
    RefBuilder ref_build(build_opts.input_list, build_opts.output_prefix, build_opts.use_rcomp);
    DONE_LOG((std::chrono::system_clock::now() - start));

    // Determine the paths to the BigBWT executables
    HelperPrograms helper_bins;
    if (!std::getenv("PFPDOC_BUILD_DIR")) {FATAL_ERROR("Need to set PFPDOC_BUILD_DIR environment variable.");}
    helper_bins.build_paths((std::string(std::getenv("PFPDOC_BUILD_DIR")) + "/bin/").data());
    helper_bins.validate();

    // Parse the input text with BigBWT, and load it into pf object
    STATUS_LOG("build_main", "generating the prefix-free parse for given reference");
    start = std::chrono::system_clock::now();

    run_build_parse_cmd(&build_opts, &helper_bins);
    pf_parsing pf(build_opts.output_ref, build_opts.pfp_w);
    DONE_LOG((std::chrono::system_clock::now() - start));

    // Builds the BWT, SA, LCP, and document array profiles and writes to a file
    STATUS_LOG("build_main", "building bwt and doc profiles based on pfp");
    start = std::chrono::system_clock::now();
    
    pfp_lcp lcp(1, pf, build_opts.output_ref, &ref_build);
    DONE_LOG((std::chrono::system_clock::now() - start));
    std::cout << "\n";
    
    return 0;
}

int run_main(int argc, char** argv) {
    /* main method for querying the data-structure */
    if (argc == 1) return pfpdoc_run_usage();
    std::cout << "\n";

    // grab the command-line options, and validate them
    PFPDocRunOptions run_opts;
    parse_run_options(argc, argv, &run_opts);
    run_opts.validate();

    // build the doc_queries object (load data-structures)
    doc_queries doc_queries_obj (run_opts.ref_file);

    // query the doc_profiles with the given reads
    STATUS_LOG("run_main", "processing the patterns");

    auto start = std::chrono::system_clock::now();
    doc_queries_obj.query_profiles(run_opts.pattern_file);
    DONE_LOG((std::chrono::system_clock::now() - start));
    
    // write index to disk
    if (run_opts.write_to_file) {
        std::string outfile = run_opts.ref_file + ".docprofiles";
        std::string outfile_bwt = run_opts.ref_file + ".docprofiles.bwt";
        std::ofstream out(outfile), out_bwt(outfile_bwt);

        doc_queries_obj.serialize(out, out_bwt, run_opts.read_length);
        out.close(); out_bwt.close();
    }
    std::cout << "\n";
    
    return 0;
}

int info_main(int argc, char** argv) {
    /* main method for info sub-command */
    if (argc == 1) return pfpdoc_info_usage();
    std::cout << "\n";

    // grab the command-line options, and validate them
    PFPDocInfoOptions info_opts;
    parse_info_options(argc, argv, &info_opts);
    info_opts.validate();

    // build the doc_queries object (load data-structures)
    doc_queries doc_queries_obj (info_opts.ref_file, info_opts.output_path, info_opts.num_profiles);
    
    return 0;
}


void run_build_parse_cmd(PFPDocBuildOptions* build_opts, HelperPrograms* helper_bins) {
    // Generates and runs the command-line for executing the PFP of the reference 
    std::ostringstream command_stream;
    if (build_opts->threads > 0) {
        std::string curr_exe = "";
        if (build_opts->is_fasta) {curr_exe.assign(helper_bins->parse_fasta_bin);}
        else {curr_exe.assign(helper_bins->parse_bin);}

        command_stream << curr_exe << " "; //<< " -i ";
        command_stream << build_opts->output_ref << " ";
        command_stream << "-w " << build_opts->pfp_w;
        command_stream << " -p " << build_opts->hash_mod;
        command_stream << " -t " << build_opts->threads;
    }
    else {
        std::string curr_exe = "";
        command_stream << helper_bins->parseNT_bin << " "; // << " -i ";
        command_stream << build_opts->output_ref << " ";
        command_stream << "-w " << build_opts->pfp_w;
        command_stream << " -p " << build_opts->hash_mod;
    }
    if (build_opts->is_fasta) {command_stream << " -f";}

    //LOG(build_opts->verbose, "build_parse", ("Executing this command: " + command_stream.str()).data());
    auto parse_log = execute_cmd(command_stream.str().c_str());
    //OTHER_LOG(parse_log.data());
}

std::string execute_cmd(const char* cmd) {
    std::array<char, 256> buffer{};
    std::string output = "";

    std::string cmd_plus_stderr = std::string(cmd) + " 2>&1";
    FILE* pipe = popen(cmd_plus_stderr.data(), "r"); // Extract stderr as well
    if (!pipe) {FATAL_ERROR("popen() failed!");}

    try {
        std::size_t bytes;
        while ((bytes = fread(buffer.data(), sizeof(char), sizeof(buffer), pipe))) {
            output += std::string(buffer.data(), bytes);
        }
    } catch (...) {
        pclose(pipe);
        FATAL_ERROR("Error occurred while reading popen() stream.");
    }
    pclose(pipe);
    return output;
}

bool endsWith(const std::string& str, const std::string& suffix) {
    // Checks if the string ends the suffix
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
}

bool is_integer(const std::string& str) {
    /* Checks if string passed is an integer */
    std::string::const_iterator iter = str.begin();
    while (iter != str.end() && std::isdigit(*iter)) {++iter;}
    return !str.empty() && iter == str.end();
}

std::vector<std::string> split(std::string input, char delim) {
    /* Takes in a string, and splits it based on delimiters */
    std::vector<std::string> word_list;
    std::string curr_word = "";

    for (char ch: input) {
        if (ch == delim && curr_word.length()) {word_list.push_back(curr_word); curr_word = "";}
        else if (ch != delim) {curr_word += ch;}
    }
    if (curr_word.length()) {word_list.push_back(curr_word);}
    return word_list;
}

int is_file(std::string path) {
    /* Checks if the path is a valid file-path */
    std::ifstream test_file(path.data());
    if (test_file.fail()) {return 0;}
    
    test_file.close();
    return 1;
}

int is_dir(std::string path) {
    /* Checks if the directory is a valid path */
    return std::filesystem::exists(path);
}

void print_build_status_info(PFPDocBuildOptions* opts) {
    /* prints out the information being used in the current run */
    std::fprintf(stdout, "\nOverview of Parameters:\n");
    std::fprintf(stdout, "\tInput file-list: %s\n", opts->input_list.data());
    std::fprintf(stdout, "\tOutput ref path: %s\n", opts->output_ref.data());
    std::fprintf(stdout, "\tPFP window size: %d\n", opts->pfp_w);
    std::fprintf(stdout, "\tInclude rev-comp?: %d\n\n", opts->use_rcomp);
}

void parse_build_options(int argc, char** argv, PFPDocBuildOptions* opts) {
    /* parses the arguments for the build sub-command, and returns a struct with arguments */
    int c = 0;
    while ((c = getopt(argc, argv, "hf:o:w:r")) >= 0) {
        switch(c) {
            case 'h': pfpdoc_build_usage(); std::exit(1);
            case 'f': opts->input_list.assign(optarg); break;
            case 'o': opts->output_prefix.assign(optarg); break;
            case 'w': opts->pfp_w = std::atoi(optarg); break;
            case 'r': opts->use_rcomp = true; break;
            default: pfpdoc_build_usage(); std::exit(1);
        }
    }
}

void parse_run_options(int argc, char** argv, PFPDocRunOptions* opts) {
    /* parses the arguments for the run sub-command, and returns struct */
    int c = 0;
    while ((c = getopt(argc, argv, "hr:p:l:s")) >= 0) {
        switch(c) {
            case 'h': pfpdoc_run_usage(); std::exit(1);
            case 'r': opts->ref_file.assign(optarg); break;
            case 'p': opts->pattern_file.assign(optarg); break;
            case 'l': opts->read_length = std::atoi(optarg); break;
            case 's': opts->write_to_file = true; break;
            default: pfpdoc_run_usage(); std::exit(1);
        }
    }
}

void parse_info_options(int argc, char** argv, PFPDocInfoOptions* opts) {
    /* parses the arguments for the info sub-command, and returns struct */
    int c = 0;
    while ((c = getopt(argc, argv, "hr:o:n:")) >= 0) {
        switch(c) {
            case 'h': pfpdoc_run_usage(); std::exit(1);
            case 'r': opts->ref_file.assign(optarg); break;
            case 'o': opts->output_path.assign(optarg); break;
            case 'n': opts->num_profiles = std::max(0, std::atoi(optarg)); break;
            default: pfpdoc_info_usage(); std::exit(1);
        }
    }
}

int pfpdoc_build_usage() {
    /* prints out the usage information for the build method */
    std::fprintf(stdout, "\npfp_doc build - builds the document array profiles using PFP.\n");
    std::fprintf(stdout, "Usage: pfp_doc build [options]\n\n");

    std::fprintf(stdout, "Options:\n");
    std::fprintf(stdout, "\t%-10sprints this usage message\n", "-h");
    std::fprintf(stdout, "\t%-10spath to a file-list of genomes to use\n", "-f [arg]");
    std::fprintf(stdout, "\t%-10soutput prefix path if using -f option\n", "-o [arg]");
    std::fprintf(stdout, "\t%-10swindow size used for pfp (default: 10)\n", "-w [INT]");
    std::fprintf(stdout, "\t%-10sinclude the reverse-complement of sequence (default: false)\n\n", "-r");

    return 0;
}

int pfpdoc_run_usage() {
    /* prints out the usage information for the run method */
    std::fprintf(stdout, "\npfp_doc run - processes a set of reads using the document array profiles.\n");
    std::fprintf(stdout, "Usage: pfp_doc run [options]\n\n");

    std::fprintf(stdout, "Options:\n");
    std::fprintf(stdout, "\t%-10sprints this usage message\n", "-h");
    std::fprintf(stdout, "\t%-10spath to the input reference file\n", "-r [arg]");
    std::fprintf(stdout, "\t%-10spath to the pattern file\n", "-p [arg]");
    std::fprintf(stdout, "\t%-10swrite data-structures to disk\n", "-s");
    std::fprintf(stdout, "\t%-10supper-bound on read length (used to shrink size of index using -s)\n\n", "-l [arg]");
    
    return 0;
}

int pfpdoc_info_usage() {
    /* prints out the usage information for the info sub-command */
    /* prints out the usage information for the run method */
    std::fprintf(stdout, "\npfp_doc info - read in document array profiles and print information.\n");
    std::fprintf(stdout, "Usage: pfp_doc info [options]\n\n");

    std::fprintf(stdout, "Options:\n");
    std::fprintf(stdout, "\t%-10sprints this usage message\n", "-h");
    std::fprintf(stdout, "\t%-10soutput prefix used for index\n", "-r [arg]");
    std::fprintf(stdout, "\t%-10soutput file path for printing document array profiles (csv format)\n", "-o [arg]");
    std::fprintf(stdout, "\t%-10snumber of profiles to print if using -o\n\n", "-n [arg]");
    
    return 0;
}

int pfpdoc_usage() {
    /* Prints the usage information for pfp_doc */
    std::fprintf(stderr, "\npfp_doc has different sub-commands to run:\n");
    std::fprintf(stderr, "Usage: pfp_doc <sub-command> [options]\n\n");

    std::fprintf(stderr, "Commands:\n");
    std::fprintf(stderr, "\tbuild\tbuilds the document profile data-structure\n");
    std::fprintf(stderr, "\trun\truns queries with respect to the document array structure\n");
    std::fprintf(stderr, "\tinfo\tprint out information regarding this index and document array\n\n");
    return 0;
}

int main(int argc, char** argv) {
    /* main method for pfp_doc */
    std::fprintf(stdout, "pfp-doc version: %s\n", PFPDOC_VERSION);
    
    if (argc > 1) {
        if (std::strcmp(argv[1], "build") == 0) 
            return build_main(argc-1, argv+1);
        else if (std::strcmp(argv[1], "run") == 0)
            return run_main(argc-1, argv+1);
        else if (std::strcmp(argv[1], "info") == 0)
            return info_main(argc-1, argv+1);
    }
    return pfpdoc_usage();
}