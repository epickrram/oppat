/* Copyright (c) 2018 Patrick Fay
 *
 * License http://opensource.org/licenses/mit-license.php MIT License
 */

#pragma once

#ifdef EXTERN2
#undef EXTERN2
#endif
#ifdef LUA_RTNS_CPP
#define EXTERN2 
#else
#define EXTERN2 extern
#endif
EXTERN2 int lua_read_data(std::string data_filename, std::string data2_filename, std::string wait_filename, prf_obj_str &prf_obj, int verbose);
EXTERN2 double lua_derived_evt(std::string lua_file, std::string lua_rtn, std::string &evt_nm,
		std::vector <std::string> &data_cols,
		std::vector <std::string> &data_vals,
		std::vector <std::string> &new_cols,
		std::vector <std::string> &new_vals,
		int verbose);
