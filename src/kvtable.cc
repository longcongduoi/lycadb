#include "kvtable.h"

#include <string>
#include <iostream>
#include <cstring>
#include <sstream>

#include <haildb.h>

using namespace std;

KVTable::KVTable(string name) : Table(name) {

}

bool
KVTable::create() {

	ib_err_t err;
	ib_tbl_sch_t schema = NULL;

	// create schema
	if((err = ib_table_schema_create(m_name.c_str(),
			&schema, IB_TBL_COMPACT, 0)) != DB_SUCCESS) {
		return false;
	}

	// add `key` column as VARCHAR(64)
	if((err = ib_table_schema_add_col(schema, "key", IB_VARCHAR,
			IB_COL_NOT_NULL, 0, 64)) != DB_SUCCESS) {
		return false;
	}

	// add `val` column as BLOB
	if((err = ib_table_schema_add_col(schema, "val", IB_BLOB,
			IB_COL_NOT_NULL, 0, 0)) != DB_SUCCESS) {
		return false;
	}

	if(!create_primary_index(schema)) {
		return false;
	}

	// begin transaction
	return install_schema(schema);
}

bool
KVTable::set(str key, str val) {

	bool ret = false;

	ib_trx_t trx = 0;
	ib_crsr_t cursor = 0;
	ib_tpl_t row = 0;

	// get table cursor
	if(get_cursor(key, trx, cursor, row) == false) {
		return false;
	}

	if(row) { // existing value

		// look up existing value
		str old((const char*)ib_col_get_value(row, 1), ib_col_get_len(row, 1));

		if(old == val) {	// same as the new one, no need to update.
			rollback(trx, cursor, row);
			return true;
		} else {
			// convert back to string and update row.
			ret = update_row(cursor, row, val);
		}
	} else { // no value, insert new row.
		ret = insert_row(cursor, key, val);
	}

	// finish up.
	if(ret) {
		commit(trx, cursor, row);
	} else {
		rollback(trx, cursor, row);
	}
	return ret;
}

bool
KVTable::get(str key, str *val) {

	ib_trx_t trx = 0;
	ib_crsr_t cursor = 0;
	ib_tpl_t row = 0;

	// get table cursor
	if(get_cursor(key, trx, cursor, row) == false) {
		return false;
	}

	if(row) { // found value

		// copy to output parameter
		*val = str((const char*)ib_col_get_value(row, 1), ib_col_get_len(row, 1), 1);

		commit(trx, cursor, row);
		return true;
	} else {
		rollback(trx, cursor, row);
		return false;
	}
}

bool
KVTable::incr(str key, int by, int &out) {

	bool ret = false;
	ib_trx_t trx = 0;
	ib_crsr_t cursor = 0;
	ib_tpl_t row = 0;

	if(get_cursor(key, trx, cursor, row) == false) {
		return false;
	}

	if(row) {	// increment
		stringstream ss;
		int i = 0;

		// read value
		str s((const char*)ib_col_get_value(row, 1), ib_col_get_len(row, 1));
		if(s.empty()) {
			i = 0;
		} else {
			ss.write(s.c_str(), s.size());

			// convert to number and increment
			ss >> i;
			ss.clear();
			ss.str("");
		}

		// save into output parameter.
		out = i + by;

		// convert back to string.
		ss << out;
		string tmp = ss.str();

		// update row.
		str s_new(tmp.c_str(), tmp.size(), 1);
		ret = update_row(cursor, row, s_new);
		s_new.reset();	// free memory.

	} else { // insert with value = "1".
		ret = insert_row(cursor, key, "1");
		out = 1;
	}

	// finish up.
	if(ret) {
		commit(trx, cursor, row);
	} else {
		rollback(trx, cursor, row);
	}
	return ret;
}

bool
KVTable::decr(str key, int by, int &out) {
	return incr(key, -by, out);
}
