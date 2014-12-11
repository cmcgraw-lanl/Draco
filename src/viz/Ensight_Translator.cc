//----------------------------------*-C++-*----------------------------------//
/*!
 * \file   viz/Ensight_Translator.cc
 * \author Thomas M. Evans
 * \date   Fri Jan 21 16:36:10 2000
 * \brief  Ensight_Translator implementation file (non-templated code).
 * \note   Copyright (C) 2000-2014 Los Alamos National Security, LLC.
 *         All rights reserved.
 */
//---------------------------------------------------------------------------//
// $Id$
//---------------------------------------------------------------------------//

#include "Ensight_Translator.hh"
#include "ds++/path.hh"
#include "ds++/SystemCall.hh"
#include <iomanip>
#include <cstring>
// <cerrno> defines the last error number from the system
//          www.cplusplus.com/reference/clibrary/cerrno/errno/
#include <cerrno>

namespace rtt_viz
{

using std::endl;
using std::setw;
using std::ios;
using std::ofstream;
using std::string;
using std::setiosflags;

//---------------------------------------------------------------------------//
// PUBLIC FUNCTIONS
//---------------------------------------------------------------------------//

//---------------------------------------------------------------------------//
/*!
 * \brief Opens the geometry and variable files.
 *
 * \param icycle Cycle number for this dump.
 * \param time   Time value for this dump.
 * \param dt     Timestep at this dump.  This parameter is only used for
 *               diagnotics and is not placed in the Ensight dump.
 */
void Ensight_Translator::
open(const int        icycle,
     const double     time,
     const double     dt)
{
    Insist(! d_geom_out.is_open(),
           "Attempted to open an already open geometry file!");

    using std::string;
    using std::ostringstream;

    // Increment local dump counter and add dump time
    d_dump_times.push_back(time);
    size_t igrdump_num = d_dump_times.size();
    Check (igrdump_num < 10000);

    // create ensight postfix indicators
    ostringstream postfix_build;
    ostringstream post_number;

    if (igrdump_num < 10)
        post_number << "000" << igrdump_num;
    else if (igrdump_num < 100) 
        post_number << "00" << igrdump_num;
    else if (igrdump_num < 1000) 
        post_number << "0" << igrdump_num;
    else 
        post_number << igrdump_num;

    postfix_build << "data." << post_number.str();
    string postfix = postfix_build.str();

    // announce the graphics dump
    std::cout << ">>> ENSIGHT GRAPHICS DUMP: icycle= " << icycle 
              << " time= " << time << " dt= " << dt << std::endl
              << "dir= " << d_prefix << ", dump_number= " 
              << igrdump_num << std::endl;
    
    // write case file
    write_case();

    // >>> Open the geometry file.
    if ( (! d_static_geom ) ||
         (d_dump_times.size() == 1) )
    {

        // make output file for this timestep
        string filename  = d_geo_dir + "/";
        if ( d_static_geom ) 
            filename += "data";
        else 
            filename += postfix;

        d_geom_out.open(filename, d_binary, true);

        // write the header
        d_geom_out << "Description line 1" << endl;
        
        ostringstream s;
        s << "probtime " << time << " cycleno " << icycle;
        d_geom_out << s.str() << endl;
    
        d_geom_out << "node id given" << endl;
        d_geom_out << "element id given" << endl;
    }

    // >>> Open the vertex data files.
    d_vertex_out.resize(d_vdata_names.size());
    
    // loop over all vertex data fields and write out data for each field
    for (size_t nvd = 0; nvd < d_vdata_names.size(); nvd++)
    {
        // open file for this data
        std::string filename  = d_vdata_dirs[nvd] + "/" + postfix;
        d_vertex_out[nvd] = new Ensight_Stream(filename, d_binary);

        *d_vertex_out[nvd] << d_vdata_names[nvd] << endl;
    }

    // >>> Open the cell data files.
    d_cell_out.resize(d_cdata_names.size());
    
    // loop over all cell data fields
    for (size_t ncd = 0; ncd < d_cdata_names.size(); ncd++)
    {
        // open file for this data
        std::string filename  = d_cdata_dirs[ncd] + "/" + postfix;
        d_cell_out[ncd] = new Ensight_Stream(filename, d_binary);

        *d_cell_out[ncd] << d_cdata_names[ncd] << endl;
    }
}

//---------------------------------------------------------------------------//
/*!
 * \brief Closes any open file streams.
 *
 * Calling this function is unnecessary if this object is destroyed.
 */
void Ensight_Translator::
close()
{
    if ( d_geom_out.is_open() ) d_geom_out.close();

    for ( size_t i = 0; i < d_vertex_out.size(); i++ )
    {
        if ( d_vertex_out[i]->is_open() ) d_vertex_out[i]->close();
    }

    for ( size_t i = 0; i < d_cell_out.size(); i++ )
    {
        if ( d_cell_out[i]->is_open() ) d_cell_out[i]->close();
    }
}

//---------------------------------------------------------------------------//
// PRIVATE FUNCTIONS
//---------------------------------------------------------------------------//

//---------------------------------------------------------------------------//
/*!
 * \brief Creates some of the file prefixes and filenames for ensight dump.
 *
 * \param prefix std_string giving the name of the problem
 * \param gd_wpath directory where dumps are stored
 */
void Ensight_Translator::create_filenames(const std_string &prefix)
{
    if( d_dump_dir[d_dump_dir.size()-1] == rtt_dsxx::UnixDirSep ||
        d_dump_dir[d_dump_dir.size()-1] == rtt_dsxx::WinDirSep )
    {
        // ensight directory name
        d_prefix = d_dump_dir + prefix + "_ensight";
    }
    else
    {
        // ensight directory name
        d_prefix = d_dump_dir + "/" + prefix + "_ensight";
    }
    // case file name
    d_case_filename = d_prefix + rtt_dsxx::dirSep + prefix + ".case";
}

//---------------------------------------------------------------------------//
/*!
 * \brief Common initializer for constructors.
 *
 * \param graphics_continue If true, use existing ensight directory.
 * If false, create or wipe out the existing directory.
 */
void Ensight_Translator::initialize(const bool graphics_continue)
{
    using std::strerror;

    d_num_cell_types = 15;

    // Assign values to d_cell_names. These are
    // the official "Ensight" names that must be used in 
    // the Ensight file.
    d_cell_names.resize(d_num_cell_types);
    d_cell_names[0]  = "point";
    d_cell_names[1]  = "bar2";
    d_cell_names[2]  = "bar3";
    d_cell_names[3]  = "tria3";
    d_cell_names[4]  = "tria6";
    d_cell_names[5]  = "quad4";
    d_cell_names[6]  = "quad8";
    d_cell_names[7]  = "tetra4";
    d_cell_names[8]  = "tetra10";
    d_cell_names[9]  = "pyramid5";
    d_cell_names[10] = "pyramid13";
    d_cell_names[11] = "hexa8";
    d_cell_names[12] = "hexa20";
    d_cell_names[13] = "penta6";
    d_cell_names[14] = "penta15";

    // Assign values to vrtx_count, the number of vertices 
    // in a cell.
    d_vrtx_cnt.resize(d_num_cell_types);
    d_vrtx_cnt[0]  = 1;
    d_vrtx_cnt[1]  = 2;
    d_vrtx_cnt[2]  = 3;
    d_vrtx_cnt[3]  = 3;
    d_vrtx_cnt[4]  = 6;
    d_vrtx_cnt[5]  = 4;
    d_vrtx_cnt[6]  = 8;
    d_vrtx_cnt[7]  = 4;
    d_vrtx_cnt[8]  = 10;
    d_vrtx_cnt[9]  = 5;
    d_vrtx_cnt[10] = 13;
    d_vrtx_cnt[11] = 8;
    d_vrtx_cnt[12] = 20;
    d_vrtx_cnt[13] = 6;
    d_vrtx_cnt[14] = 15;

    // Assign values to d_cell_type_index. The user will
    // use these to identify cell types.
    d_cell_type_index.resize(d_num_cell_types);
    d_cell_type_index[0]  = point;
    d_cell_type_index[1]  = two_node_bar;
    d_cell_type_index[2]  = three_node_bar;
    d_cell_type_index[3]  = three_node_triangle;
    d_cell_type_index[4]  = six_node_triangle;
    d_cell_type_index[5]  = four_node_quadrangle;
    d_cell_type_index[6]  = eight_node_quadrangle;
    d_cell_type_index[7]  = four_node_tetrahedron;
    d_cell_type_index[8]  = ten_node_tetrahedron;
    d_cell_type_index[9]  = five_node_pyramid;
    d_cell_type_index[10] = thirteen_node_pyramid;
    d_cell_type_index[11] = eight_node_hexahedron;
    d_cell_type_index[12] = twenty_node_hexahedron;
    d_cell_type_index[13] = six_node_wedge;
    d_cell_type_index[14] = fifteen_node_wedge;

    // Check d_dump_dir
    rtt_dsxx::draco_getstat dumpDirStat( d_dump_dir );
    //struct stat sbuf;
    //int stat_ret = stat(d_dump_dir.c_str(), &sbuf);
    if( ! dumpDirStat.isdir() )
    {
        std::ostringstream dir_error;
        dir_error << "Error opening dump directory \"" 
                        << d_dump_dir << "\": " << strerror(errno);
        Insist( dumpDirStat.isdir(),  dir_error.str() );
    }

    // try to create d_prefix
    rtt_dsxx::draco_mkdir( d_prefix );
    rtt_dsxx::draco_getstat prefixDirStat( d_prefix );
    // stat_ret = stat(d_prefix.c_str(), &sbuf);
    if( ! prefixDirStat.isdir() )
    {
        std::ostringstream dir_error;
        dir_error << "Unable to create EnSight directory \"" 
                  << d_prefix << "\": " << strerror(errno);
        Insist ( dumpDirStat.isdir(),  dir_error.str() );
    }

    // See if the case file exists
    struct stat sbuf;
    int stat_ret = stat(d_case_filename.c_str(), &sbuf);    
    
    // build the ensight directory if this is not a continuation
    if (!graphics_continue)
    { 
        // We have guaranteed that our prefix directory exists at this
        // point.  Now, wipe out files that we might have created in there...
        if(!stat_ret)
        {
            rtt_dsxx::draco_remove_dir( d_prefix );
            rtt_dsxx::draco_mkdir(      d_prefix );
        }
    }
    else
    {
        // We were asked for a continuation.  Complain if we don't have a
        // case file.
        if(stat_ret)
        {
            std::ostringstream dir_error;
            dir_error << "EnSight directory \""
                      << d_prefix << "\" doesn't contain a case file!";
            Insist (0,  dir_error.str().c_str());
        }

    }
       
    // Check to make sure the variable names are of acceptable length 
    // and contain no forbidden characters. Ensight prohibits the
    // characters "( ) [ ] + - @ ! # * ^ $ / space", and requires
    // the names be 19 characters or less. Moreover, since these
    // names will be used to label output and to create directories,
    // the names should also be unique.

    size_t nvdata = d_vdata_names.size();
    size_t ncdata = d_cdata_names.size();

    typedef std::vector<sf_string::iterator> SFS_iter_vec;
    // Create a name list for testing.
    sf_string name_tmp(nvdata+ncdata);
    for (size_t i=0; i < nvdata; i++ )
        name_tmp[i] = d_vdata_names[i];
    for (size_t i=0; i < ncdata; i++ )
        name_tmp[i+nvdata] = d_cdata_names[i];
    // Check for name lengths out of limits
    {
        int low = 1;
        int high= 19;
        SFS_iter_vec result =
            rtt_dsxx::check_string_lengths(name_tmp.begin(), 
                                           name_tmp.end(), low, high);
        if (result.size() != 0) 
        {
            std::cerr << "*** Error in variable name(s) -" << std::endl;
            for (size_t i=0; i<result.size(); i++)
                std::cerr << "Size of name is not in allowable range: \"" 
                          << *result[i] << "\"" << std::endl;
            std::cerr << "Name lengths must be greater than " << low 
                      << " and less than " << high << "." << std::endl;
            Insist (0, "Ensight variable name length out of limits!");
        }
    }
    // Check for bad characters.
    {
        std::string bad_chars = "()[]+-@!#*^$/ ";
        SFS_iter_vec result =
            rtt_dsxx::check_string_chars(name_tmp.begin(), 
                                         name_tmp.end(), bad_chars);
        if (result.size() != 0) 
        {
            std::cerr << "*** Error in variable name(s) -" << std::endl;
            for (size_t i=0; i<result.size(); i++) 
                std::cerr << "Found disallowed character(s) in name: \"" 
                          << *result[i] << "\"" << std::endl;
            std::cerr << "The following characters are forbidden:" << 
                std::endl << " \"" << bad_chars << "\"," << 
                " as well as any white-space characters." << std::endl;
            Insist (0, "Found illegal character in ensight variable names!");
        }
    }
    // Check for non-unique names
    {
        SFS_iter_vec result =
            rtt_dsxx::check_strings_unique(name_tmp.begin(), name_tmp.end());
        if (result.size() != 0) 
        {
            std::cerr << "*** Error in variable name(s) -" << std::endl;
            for (size_t i=0; i<result.size(); i++)
                std::cerr << "Duplicate name found: \"" 
                          << *result[i] << "\"" << std::endl;
            std::cerr << "All variable names must be unique!" << std::endl;
            Insist (0, "Duplicate ensight variable names found!");
        }
    }

    // calculate and make the geometry directory if this is not a
    // continuation
    d_geo_dir = d_prefix + "/geo";
    if (!graphics_continue)
        rtt_dsxx::draco_mkdir( d_geo_dir );

    // make data directory names and directories
    d_vdata_dirs.resize(d_vdata_names.size());
    d_cdata_dirs.resize(d_cdata_names.size());
    for (size_t i = 0; i < d_vdata_names.size(); i++)
    {
        d_vdata_dirs[i] = d_prefix + "/" + d_vdata_names[i];
        
        // if this is not a continuation make the directory
        if (!graphics_continue)
            rtt_dsxx::draco_mkdir( d_vdata_dirs[i] );
    }
    for (size_t i = 0; i < d_cdata_names.size(); i++)
    {
        d_cdata_dirs[i] = d_prefix + "/" + d_cdata_names[i];

        // if this is not a continuation make the directory
        if (!graphics_continue)
            rtt_dsxx::draco_mkdir( d_cdata_dirs[i] );
    }   
}

//---------------------------------------------------------------------------//
/*!
 * \brief Write out case file.
 */
void Ensight_Translator::write_case()
{
    // create the case file name (directory already created)
    const char *filename = d_case_filename.c_str();
    
    // open the case file
    ofstream caseout(filename);

    // write the format header
    caseout << "FORMAT" << endl;
    caseout << "type: ensight gold" << endl << endl;

    // write the geometry file block
    caseout << "GEOMETRY" << endl;
    if ( d_static_geom )
        caseout << "model: ./geo/data";
    else
        caseout << "model: 1   ./geo/data.****";
    caseout << endl << endl;

    // write the variable block header
    caseout << "VARIABLE" << endl;

    // write the pointer to the node variables
    for (size_t i = 0; i < d_vdata_names.size(); i++)
        caseout << "scalar per node:    1  " << setw(19)
                << setiosflags(ios::left)
                << d_vdata_names[i] << setw(4) << " "
                << "./" << d_vdata_names[i] << "/data.****" << endl;

    // write the pointer to the cell variables
    for (size_t i = 0; i < d_cdata_names.size(); i++)
        caseout << "scalar per element: 1  " << setw(19)
                << setiosflags(ios::left)
                << d_cdata_names[i] << setw(4) << " "
                << "./" << d_cdata_names[i] << "/data.****" << endl;

    caseout << endl;
    // write out the time block
    caseout << "TIME" << endl;
    caseout << "time set:              " << setw(4) << "   1" << endl;
    caseout << "number of steps:       " << setw(4) << setiosflags(ios::right) 
            << d_dump_times.size() << endl;
    caseout << "filename start number: " << setw(4) << "   1" << endl;
    caseout << "filename increment:    " << setw(4) << "   1" << endl;
    caseout << "time values:           " << endl;
    
    // write out times
    caseout.precision(5);
    caseout.setf(ios::scientific, ios::floatfield);
    for (size_t i = 0; i < d_dump_times.size(); i++)
        caseout << setw(12) << setiosflags(ios::right) << d_dump_times[i] 
                << endl;
}

} // end of rtt_viz

//---------------------------------------------------------------------------//
// end of Ensight_Translator.cc
//---------------------------------------------------------------------------//
