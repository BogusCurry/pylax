﻿// DynasetObject.c  | Pylax © 2017 by Thomas Führinger
#include "Pylax.h"

// Forward declarations
static BOOL PxDynaset_UpdateControlWidgets(PxDynasetObject* self);
static BOOL PxDynaset_SetFlagAndPropagateDown(PxDynasetObject* self, size_t nOffset, BOOL bFlag);

static PyStructSequence_Field PxDynasetColumnFields[] = {
	{ "name", "Name of column in query" },
	{ "index", "Position in query" },
	{ "type", "Data type" },
	{ "key", "True if column is part of the primary key, False if non-key database column, None if not part of the database table" },
	{ "default", "Default value" },
	{ "get_default", "Function providing default value" },
	{ "format", "Default display format" },
	{ "parent", "Coresponding column in parent Dynaset" },
	{ NULL }
};

static PyStructSequence_Desc PxDynasetColumnDesc = {
	"DynasetColumn",
	NULL,
	PxDynasetColumnFields,
	8
};

static PyStructSequence_Field PxDynasetRowFields[] = {
	{ "data", "Tuple of data pulled" },
	{ "dataOld", "Tuple of data before modification" },
	{ "new", "True if row is still not in database" },
	{ "delete", "True row to be removed from the database" },
	{ NULL }
};

static PyStructSequence_Desc PxDynasetRowDesc = {
	"DynasetRow",
	NULL,
	PxDynasetRowFields,
	4
};

BOOL PxDynasetTypes_Init()
{
	if (PxDynasetColumnType.tp_name == 0)
		PyStructSequence_InitType(&PxDynasetColumnType, &PxDynasetColumnDesc);
	Py_INCREF(&PxDynasetColumnType);
	if (PxDynasetRowType.tp_name == 0)
		PyStructSequence_InitType(&PxDynasetRowType, &PxDynasetRowDesc);
	Py_INCREF(&PxDynasetRowType);
	return TRUE;
}

static PyObject *
PxDynaset_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PxDynasetObject* self = (PxDynasetObject *)type->tp_alloc(type, 0);
	if (self != NULL) {
		self->pyParent = NULL;
		self->pyConnection = NULL;
		self->pyTable = NULL;
		self->bHasWhoCols = TRUE;
		self->pyQuery = NULL;
		self->pyCursor = NULL;
		self->strInsertSQL = NULL;
		self->strUpdateSQL = NULL;
		self->strDeleteSQL = NULL;
		self->pyAutoColumn = NULL;
		self->pyParams = NULL;
		self->pyEmptyRowData = NULL;
		self->pyColumns = NULL;
		self->pyRows = NULL;
		self->nRows = 0;
		self->nRow = -1;
		self->nRowEnd = -1;
		self->iLastRowID = -1;
		self->pyParent = NULL;
		self->bAutoExecute = TRUE;
		self->bFrozen = FALSE;
		self->bLocked = TRUE;
		self->bDirty = FALSE;
		self->bFrozenIfDirty = TRUE;
		self->bBroadcasting = FALSE;
		self->pySearchButton = NULL;
		self->pyNewButton = NULL;
		self->pyEditButton = NULL;
		self->pyUndoButton = NULL;
		self->pySaveButton = NULL;
		self->pyDeleteButton = NULL;
		self->pyOkButton = NULL;
		self->pyWidgets = NULL;
		self->pyChildren = NULL;
		self->pyOnParentSelectionChangedCB = NULL;
		self->pyBeforeSaveCB = NULL;

		return (PyObject*)self;
	}
	else
		return NULL;
}

static int
PxDynaset_init(PxDynasetObject *self, PyObject *args, PyObject *kwds)
{
	//OutputDebugString(L"\n-Dynaset init-\n");
	static char *kwlist[] = { "table", "query", "parent", "cnx", NULL };
	PyObject* pyTable = NULL, *pyQuery = NULL, *pyParent = NULL, *pyConnection = NULL, *tmp;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOO", kwlist,
		&pyTable,
		&pyQuery,
		&pyParent,
		&pyConnection))
		return -1;

	if (PyUnicode_Check(pyTable)) {
		tmp = self->pyTable;
		Py_INCREF(pyTable);
		self->pyTable = pyTable;
		Py_XDECREF(tmp);
	}
	else {
		PyErr_Format(PyExc_TypeError, "Parameter 1 ('table') must be string, not '%.200s'.", pyTable->ob_type->tp_name);
		return -1;
	}

	if (pyQuery) {
		if (PyUnicode_Check(pyQuery)) {
			tmp = self->pyQuery;
			Py_INCREF(pyQuery);
			self->pyQuery = pyQuery;
			Py_XDECREF(tmp);
		}
		else {
			PyErr_Format(PyExc_TypeError, "Parameter 2 ('query') must be string, not '%.200s'.", pyQuery->ob_type->tp_name);
			return -1;
		}
	}
	else
		self->pyQuery = NULL;

	if (pyParent) {
		if (PyObject_TypeCheck(pyParent, &PxDynasetType)) {
			tmp = (PyObject*)self->pyParent;
			Py_INCREF(pyParent);
			self->pyParent = (PxDynasetObject*)pyParent;
			Py_XDECREF(tmp);
			if (PyList_Append(self->pyParent->pyChildren, (PyObject*)self) == -1)
				return -1;
		}
		else {
			PyErr_Format(PyExc_TypeError, "Parameter 3 ('parent') must be a Dynaset, not '%.200s'.", pyParent->ob_type->tp_name);
			return -1;
		}
	}

	if (pyConnection) {
		//if (PyObject_TypeCheck(pyConnection, &pysqlite_ConnectionType)) {
		if (TRUE) {
			tmp = self->pyConnection;
			Py_INCREF(pyConnection);
			self->pyConnection = pyConnection;
			Py_XDECREF(tmp);
		}
		else {
			PyErr_Format(PyExc_TypeError, "Parameter 4 ('cnx') must be a SQLite connection, not '%.200s'.", pyConnection->ob_type->tp_name);
			return -1;
		}
	}
	else {
		Py_INCREF(g.pyConnection);
		self->pyConnection = g.pyConnection;
	}

	if ((self->pyRows = PyList_New(0)) == NULL) {
		PyErr_SetString(PyExc_RuntimeError, "Can not create list of rows.");
		return -1;
	}
	if ((self->pyColumns = PyDict_New()) == NULL) {
		PyErr_SetString(PyExc_RuntimeError, "Can not create dict of columns.");
		return -1;
	}
	if ((self->pyWidgets = PyList_New(0)) == NULL) {
		PyErr_SetString(PyExc_RuntimeError, "Can not create list of widgets.");
		return -1;
	}
	if ((self->pyChildren = PyList_New(0)) == NULL) {
		PyErr_SetString(PyExc_RuntimeError, "Can not create list of children.");
		return -1;
	}

	//OutputDebugString(L"\n-Dynaset init out-\n");
	return 0;
}

static PyObject* // new ref
PxDynaset_add_column(PxDynasetObject* self, PyObject *args, PyObject *kwds)
{
	static char *kwlist[] = { "name", "type", "key", "format", "default", "defaultFunction", "parent", NULL };
	PyObject* pyName = NULL, *pyType = NULL, *pyKey = NULL, *pyFormat = NULL, *pyDefault = NULL, *pyDefaultFunction = NULL, *pyParent = NULL, *pyParentColumn = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOOOOO", kwlist,
		&pyName,
		&pyType,
		&pyKey,
		&pyFormat,
		&pyDefault,
		&pyDefaultFunction,
		&pyParent))
		return NULL;

	if (!PyUnicode_Check(pyName)) {
		PyErr_SetString(PyExc_TypeError, "Parameter 1 ('name') must be a string.");
		return NULL;
	}

	if (pyType) {
		if (!PyType_Check(pyType)) {
			PyErr_SetString(PyExc_TypeError, "Parameter 2 ('type') must be a Type Object.");
			return NULL;
		}
	}
	else {
		pyType = (PyObject*)&PyUnicode_Type;
	}

	if (pyKey) {
		if (pyKey != Py_True && pyKey != Py_False && pyKey != Py_None) {
			PyErr_SetString(PyExc_TypeError, "Parameter 3 ('key') must be a boolean or None.");
			return NULL;
		}
	}
	else {
		pyKey = Py_False;
	}

	if (pyFormat) {
		if (!PyUnicode_Check(pyFormat)) {
			PyErr_SetString(PyExc_TypeError, "Parameter 4 ('format') must be a string.");
			return NULL;
		}
	}
	else {
		pyFormat = Py_None;
	}

	pyDefault = pyDefault ? pyDefault : Py_None;


	if (pyDefaultFunction) {
		if (!PyCallable_Check(pyDefaultFunction)) {
			PyErr_SetString(PyExc_TypeError, "Parameter 5 ('defaultFunction') must be a callable object.");
			return NULL;
		}
	}
	else
		pyDefaultFunction = Py_None;


	if (pyParent) {
		if (PyObject_TypeCheck(pyParent, &PxDynasetType))
			pyParentColumn = pyParent;
		else if (PyUnicode_Check(pyParent)) {
			pyParentColumn = PyDict_GetItem(self->pyParent->pyColumns, pyParent);
			if (!pyParentColumn)
				return PyErr_Format(PyExc_AttributeError, "Parent Dynaset has no column named '%s'.", PyUnicode_AsUTF8(pyParent));
		}
		else {
			PyErr_SetString(PyExc_TypeError, "Parameter 6 ('parent') must be a Dynaset or a str.");
			return NULL;
		}
	}
	else {
		pyParentColumn = Py_None;
	}

	Py_INCREF(pyName);
	Py_INCREF(Py_None);
	Py_INCREF(pyType);
	Py_INCREF(pyKey);
	Py_INCREF(pyFormat);
	Py_INCREF(pyDefault);
	Py_INCREF(pyDefaultFunction);
	Py_INCREF(pyParentColumn);

	PyObject* pyColumn = PyStructSequence_New(&PxDynasetColumnType);
	PyStructSequence_SET_ITEM(pyColumn, PXDYNASETCOLUMN_NAME, pyName);
	PyStructSequence_SET_ITEM(pyColumn, PXDYNASETCOLUMN_INDEX, Py_None);
	PyStructSequence_SET_ITEM(pyColumn, PXDYNASETCOLUMN_TYPE, pyType);
	PyStructSequence_SET_ITEM(pyColumn, PXDYNASETCOLUMN_KEY, pyKey);
	PyStructSequence_SET_ITEM(pyColumn, PXDYNASETCOLUMN_FORMAT, pyFormat);
	PyStructSequence_SET_ITEM(pyColumn, PXDYNASETCOLUMN_DEFAULT, pyDefault);
	PyStructSequence_SET_ITEM(pyColumn, PXDYNASETCOLUMN_DEFFUNC, pyDefaultFunction);
	PyStructSequence_SET_ITEM(pyColumn, PXDYNASETCOLUMN_PARENT, pyParentColumn);

	//Py_INCREF(pyName);
	if (PyDict_SetItem(self->pyColumns, pyName, pyColumn) == -1) {
		return NULL;
	}
	return(pyColumn);
}

static PyObject* // new ref
PxDynaset_get_column(PxDynasetObject* self, PyObject *args)
{
	PyObject* pyColumnName, *pyColumn;
	if (!PyArg_ParseTuple(args, "O", &pyColumnName)) {
		return NULL;
	}
	pyColumn = PyDict_GetItem(self->pyColumns, pyColumnName);
	Py_INCREF(pyColumn);
	return pyColumn;
}

PyObject* // borrowed ref
PxDynaset_GetData(PxDynasetObject* self, Py_ssize_t nRow, PyObject* pyColumn)
{
	if (nRow == -1)
		nRow = self->nRow;
	if (nRow < 0 || nRow > self->nRows) {
		PyErr_SetString(PyExc_IndexError, "Row number out of range.");
		return NULL;
	}

	Py_ssize_t nColumn = PyLong_AsSsize_t(PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_INDEX));
	PyObject* pyRow = PyList_GetItem(self->pyRows, nRow);
	PyObject* pyRowData = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATA);
	PyObject* pyDataItem = PyTuple_GetItem(pyRowData, nColumn);
	return pyDataItem;
}

static PyObject* // new ref
PxDynaset_get_data(PxDynasetObject* self, PyObject *args)
{
	Py_ssize_t nRow = self->nRow;
	PyObject* pyColumn, *pyData;
	if (!PyArg_ParseTuple(args, "O|n", &pyColumn, &nRow)) {
		return NULL;
	}
	if (nRow < -1 || nRow > self->nRows) {
		PyErr_SetString(PyExc_IndexError, "Row number out of range.");
		return NULL;
	}

	if (PyUnicode_Check(pyColumn)) {
		pyData = pyColumn;
		pyColumn = PyDict_GetItem(self->pyColumns, pyData);
		if (!pyColumn)
			return PyErr_Format(PyExc_AttributeError, "Dynaset has no column named '%s'.", PyUnicode_AsUTF8(pyData));
	}
	pyData = PxDynaset_GetData(self, nRow, pyColumn);

	if (pyData == NULL)
		return NULL;
	Py_INCREF(pyData);
	return pyData;
}

BOOL
PxDynaset_SetData(PxDynasetObject* self, Py_ssize_t nRow, PyObject* pyColumn, PyObject* pyData)
{
	Py_ssize_t nColumn = PyLong_AsSsize_t(PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_INDEX));
	PyObject* pyRow = PyList_GetItem(self->pyRows, nRow);
	PyObject* pyRowData = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATA);
	PyObject* pyRowDataOld = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATAOLD);
	PyObject* pyRowNew = PyStructSequence_GetItem(pyRow, PXDYNASETROW_NEW);
	PyObject* pyDataOld = NULL;
	OutputDebugString(L"\n*---- PxDynaset_SetData");

	if (pyRowDataOld == Py_None && pyRowNew == FALSE) { // keep a copy of the original data tuple
		//pyRowDataOld = PyObject_Copy(pyRowData);
		Py_ssize_t nSize = PyTuple_Size(pyRowData);
		pyRowDataOld = PyTuple_GetSlice(pyRowData, 0, nSize - 1); // make a copy
		Py_DECREF(Py_None);
		//ShowInts(L"X", iColumn, pyRowData->ob_refcnt);
		PyStructSequence_SetItem(pyRow, PXDYNASETROW_DATAOLD, pyRowDataOld);
	}
	//Py_DECREF(pyRowData);
	//ShowInts(L"X", nColumn, pyRowData->ob_refcnt);
	pyDataOld = PyTuple_GetItem(pyRowData, nColumn);
	//ShowInts(L"X", nColumn, nRow);
	/*
			PyObject* pyDataType = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_TYPE);
			if (pyDataType==&PxImageType)
			pyData = PxImage_AsBytes(pyData);
			else
			Py_INCREF(pyData);*/

	if (PyTuple_SetItem(pyRowData, nColumn, pyData) != 0) {
		//PyErr_SetString(PyExc_RuntimeError, "Can not set data in row.");
		return FALSE;
	}
	//PyTuple_SET_ITEM(pyRowData, iColumn, pyData);
	Py_XDECREF(pyDataOld);
	self->bDirty = TRUE;
	if (self->pyParent)
		PxDynaset_Freeze(self->pyParent);

	return PxDynaset_DataChanged(self, nRow, pyColumn);
}

static PyObject* // new ref
PxDynaset_set_data(PxDynasetObject* self, PyObject *args)
{
	Py_ssize_t nRow = self->nRow;
	PyObject* pyColumn, *pyData;
	if (!PyArg_ParseTuple(args, "OO|n", &pyColumn, &pyData, &nRow)) {
		return NULL;
	}
	if (nRow < -1 || nRow > self->nRows) {
		PyErr_SetString(PyExc_IndexError, "Row number out of range.");
		return NULL;
	}

	if (PyUnicode_Check(pyColumn)) {
		pyData = pyColumn;
		pyColumn = PyDict_GetItem(self->pyColumns, pyData);
		if (!pyColumn)
			return PyErr_Format(PyExc_AttributeError, "Dynaset has no column named '%s'.", PyUnicode_AsUTF8(pyData));
	}

	if (!PxDynaset_SetData(self, nRow, pyColumn, pyData))
		return NULL;
	Py_RETURN_NONE;
}

static PyObject* // new ref
PxDynaset_get_row(PxDynasetObject* self, PyObject *args)
{
	Py_ssize_t nRow = self->nRow;
	PyObject* pyRow = NULL;

	if (!PyArg_ParseTuple(args, "|n", &nRow)) {
		return NULL;
	}
	if (nRow < 0)
		Py_RETURN_NONE;
	else if (nRow > self->nRows) {
		PyErr_SetString(PyExc_IndexError, "Row number out of range.");
		return NULL;
	}
	pyRow = PyList_GetItem(self->pyRows, nRow);
	if (pyRow) {
		Py_INCREF(pyRow);
		return pyRow;
	}
	else
		return NULL;
}

static PyObject* // new ref
PxDynaset_get_row_data(PxDynasetObject* self, PyObject *args)
{
	Py_ssize_t nRow = self->nRow;
	PyObject* pyRow = NULL;

	if (!PyArg_ParseTuple(args, "|n", &nRow)) {
		return NULL;
	}
	if (nRow < 0)
		Py_RETURN_NONE;
	else if (nRow > self->nRows) {
		PyErr_SetString(PyExc_IndexError, "Row number out of range.");
		return NULL;
	}
	return PxDynaset_GetRowDataDict(self, nRow, FALSE);
}

BOOL
PxDynaset_Clear(PxDynasetObject* self)
{
	if (self->nRows <= 0)
		return TRUE;

	if (self->bFrozen) {
		PyErr_SetString(PyExc_RuntimeError, "Dynaset is frozen.");
		return FALSE;
	}
	else if (!PxDynaset_SetRow(self, -1))
		return FALSE;

	Py_DECREF(self->pyRows);
	self->pyRows = PyList_New(0);
	self->nRows = 0;
	Py_XDECREF(self->pyEmptyRowData);
	self->pyEmptyRowData = NULL;

	if (!PxDynaset_DataChanged(self, -1, NULL))
		return FALSE;
	return TRUE;
}

static PyObject* // new ref
PxDynaset_clear(PxDynasetObject* self, PyObject *args)
{
	if (!PxDynaset_Clear(self))
		return NULL;
	Py_RETURN_NONE;
}

PyObject* // new ref
PxDynaset_execute(PxDynasetObject* self, PyObject *args, PyObject *kwds)
{
	static char *kwlist[] = { "parameters", "query", NULL };
	PyObject* pyParameters = NULL, *pyQuery = NULL, *pyResult = NULL, *tmp = NULL;
	if (args && !PyArg_ParseTupleAndKeywords(args, kwds, "|OO", kwlist, &pyParameters, &pyQuery)) {
		return NULL;
	}

	if (pyParameters) {
		if (!PyDict_Check(pyParameters)) {
			PyErr_SetString(PyExc_TypeError, "Parameter 1 ('parameters') must be a dict.");
			return NULL;
		}
	}
	else if (self->pyParent) {
		PyObject* pyColumn, *pyParentDynasetColumn, *pyColumnName, *pyData;
		Py_ssize_t nPos = 0;
		pyParameters = PyDict_New();

		while (PyDict_Next(self->pyColumns, &nPos, &pyColumnName, &pyColumn)) {
			if (PyErr_Occurred()) {
				PyErr_Print();
				return NULL;
			}

			pyParentDynasetColumn = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_PARENT);
			if (pyParentDynasetColumn != Py_None) {
				pyData = PxDynaset_GetData(self->pyParent, self->pyParent->nRow, pyParentDynasetColumn);
				if (PyDict_SetItem(pyParameters, pyColumnName, pyData) == -1) {
					return NULL;
				}
			}
		}

		if (PyDict_Size(pyParameters) == 0) {
			Py_DECREF(pyParameters);
			pyParameters = NULL;
		}
	}

	if (pyQuery) {
		if (PyUnicode_Check(pyQuery)) {
			tmp = self->pyQuery;
			Py_INCREF(pyQuery);
			self->pyQuery = pyQuery;
			Py_XDECREF(tmp);
		}
		else {
			PyErr_SetString(PyExc_TypeError, "Parameter 2 ('query') must be a string.");
			return NULL;
		}
	}

	if (!PxDynaset_Clear(self))
		return NULL;

	if (self->pyCursor && PyObject_CallMethod(self->pyCursor, "close", NULL) == NULL)
		return NULL;

	if ((self->pyCursor = PyObject_CallMethod(self->pyConnection, "cursor", NULL)) == NULL) {
		return NULL;
	}

	const char* strQuery = PyUnicode_AsUTF8(self->pyQuery);
	if (pyParameters)
		pyResult = PyObject_CallMethod(self->pyCursor, "execute", "(sO)", strQuery, pyParameters);
	else
		pyResult = PyObject_CallMethod(self->pyCursor, "execute", "(s)", strQuery);
	if (pyResult == NULL) {
		return NULL;
	}
	//MessageBox(NULL, L"pyParent!", L"Error", MB_ICONERROR);

	PyObject* pyColumnDescriptions = PyObject_GetAttrString(self->pyCursor, "description");
	PyObject* pyIterator = PyObject_GetIter(pyColumnDescriptions);
	PyObject* pyItem, *pyColumnName, *pyColumn, *pyRow, *pyIndex;
	Py_ssize_t nIndex = 0;
	if (pyIterator == NULL) {
		return NULL;
	}

	// make my columns' index numbers point to correct position in query result tuples
	while (pyItem = PyIter_Next(pyIterator)) {
		pyColumnName = PyTuple_GetItem(pyItem, 0);
		if (pyColumnName == NULL)
			return NULL;

		pyColumn = PyDict_GetItem(self->pyColumns, pyColumnName);
		if (pyColumn != NULL) {
			pyIndex = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_INDEX);
			Py_DECREF(pyIndex);
			PyStructSequence_SetItem(pyColumn, PXDYNASETCOLUMN_INDEX, PyLong_FromSsize_t(nIndex));
		}
		else {
			return PyErr_Format(PyExc_AttributeError, "Column '%s' of query not contained in Dynaset's column list.", PyUnicode_AsUTF8(pyColumnName));
		}
		nIndex++;
		Py_DECREF(pyItem);
	}
	Py_DECREF(pyIterator);
	//PyErr_Clear();

	// create Dynaset rows and reference to query result tuples
	self->nRows = 0;
	while (pyItem = PyIter_Next(pyResult)) {
		//Py_INCREF(pyItem); // ??
		Py_INCREF(Py_None);
		Py_INCREF(Py_False);
		Py_INCREF(Py_False);
		pyRow = PyStructSequence_New(&PxDynasetRowType);
		PyStructSequence_SetItem(pyRow, PXDYNASETROW_DATA, pyItem);
		PyStructSequence_SetItem(pyRow, PXDYNASETROW_DATAOLD, Py_None);
		PyStructSequence_SetItem(pyRow, PXDYNASETROW_NEW, Py_False);
		PyStructSequence_SetItem(pyRow, PXDYNASETROW_DELETE, Py_False);

		if (PyList_Append(self->pyRows, pyRow) == -1) {
			return NULL;
		}
		self->nRows++;
	}

	Py_DECREF(pyResult);
	Py_DECREF(pyRow);
	Py_DECREF(pyColumnDescriptions);
	if (self->pyParent)
		Py_XDECREF(pyParameters);

	if (PyErr_Occurred()) {
		return NULL;
	}

	if (!PxDynaset_DataChanged(self, -1, NULL))
		return NULL;

	OutputDebugString(L"\n- Execute over\n");
	return PyLong_FromSsize_t(self->nRows);
}
/*
PyObject* // new ref
PxDynaset_GetFieldDict(PxDynasetObject* self, Py_ssize_t nRow)
{
PyObject* pyColumnName, *pyColumn, *pyRow, *pyRowData, *pyData;
Py_ssize_t nColumn, nPos = 0;
PyObject* pyKeys = PyDict_New();
pyRow = PyList_GetItem(self->pyRows, nRow);
pyRowData = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATA);

while (PyDict_Next(self->pyColumns, &nPos, &pyColumnName, &pyColumn)) {
if (PyErr_Occurred()) {
PyErr_Print();
return NULL;
}
nColumn = PyLong_AsSsize_t(PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_INDEX));
pyData = PyTuple_GetItem(pyRowData, nColumn);
if (PyDict_SetItem(pyKeys, pyColumnName, pyData) == -1) // PyDict_SetItem increfs...
return NULL;
}
return pyKeys;
}
*/
PyObject* // new ref
PxDynaset_GetRowDataDict(PxDynasetObject* self, Py_ssize_t nRow, BOOL bKeysOnly)
{
	PyObject* pyColumnName, *pyColumn, *pyRow, *pyRowData, *pyData, *pyIsKey, *pyRowDataDict;
	Py_ssize_t nColumn, nPos = 0;
	if (nRow == -1)
		nRow = self->nRow;
	if (nRow == -1)
		Py_RETURN_NONE;
	pyRow = PyList_GetItem(self->pyRows, nRow);
	pyRowData = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATA);
	pyRowDataDict = PyDict_New();

	while (PyDict_Next(self->pyColumns, &nPos, &pyColumnName, &pyColumn)) {
		if (PyErr_Occurred()) {
			PyErr_Print();
			return NULL;
		}
		nColumn = PyLong_AsSsize_t(PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_INDEX));
		pyIsKey = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_KEY);
		pyData = PyTuple_GetItem(pyRowData, nColumn);
		//PyObject_ShowRepr(pyData);
		if (!bKeysOnly || pyIsKey == Py_True)
			if (PyDict_SetItem(pyRowDataDict, pyColumnName, pyData) == -1) // PyDict_SetItem increfs...
				return NULL;
	}
	return pyRowDataDict;
}

int
PxDynaset_Save(PxDynasetObject* self)
{
	//ShowInts(L"X", self->pyBeforeSaveCB, 0);
	if (self->pyBeforeSaveCB) {
		MessageBoxA(0, "SA", "strSql", 0);
		PyObject* pyArgs = PyTuple_Pack(1, (PyObject*)self);
		//Py_INCREF(self);
		PyObject* pyResult = PyObject_CallObject(self->pyBeforeSaveCB, pyArgs);
		Py_DECREF(pyArgs);
		if (pyResult == NULL)
			return -1;
		else if (pyResult == Py_False) {
			Py_DECREF(pyResult);
			return 0;
		}
		else Py_DECREF(pyResult);
	}

	// save all descendants first
	PxDynasetObject* pyChild;
	Py_ssize_t n, nLen;
	nLen = PySequence_Size(self->pyChildren);
	for (n = 0; n < nLen; n++) {
		pyChild = (PxDynasetObject*)PyList_GetItem(self->pyChildren, n);
		if (PxDynaset_Save(pyChild) == -1)
			return -1;
	}

	// iterate over own rows
	PyObject* pyColumnName, *pyColumn, *pyRow, *pyRowData, *pyRowDataOld, *pyRowDelete, *pyRowNew, *pyData, *pyIsKey, *pyParams, *pyCursor, *pyLastRowID, *tmp;
	Py_ssize_t nRow, nColumn, nPos;
	int iRecordsChanged = 0;
	LPCSTR strSql;
	LPCSTR strSql2;

	nLen = PySequence_Size(self->pyRows);
	for (nRow = 0; nRow < nLen; nRow++) {
		pyRow = PyList_GetItem(self->pyRows, nRow);
		pyRowData = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATA);
		pyRowDataOld = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATAOLD);
		pyRowDelete = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DELETE);
		pyRowNew = PyStructSequence_GetItem(pyRow, PXDYNASETROW_NEW);
		pyParams = PyList_New(0);
		nPos = 0;
		//XX(pyRowDataOld);

		// DELETE
		if (pyRowDelete == Py_True) {
			if (pyRowNew == Py_False) {
				strSql = StringAppend(NULL, "DELETE FROM ");
				strSql = StringAppend2(strSql, PyUnicode_AsUTF8(self->pyTable), " WHERE ");

				while (PyDict_Next(self->pyColumns, &nPos, &pyColumnName, &pyColumn)) {
					if (PyErr_Occurred()) {
						PyErr_Print();
						return -1;
					}
					nColumn = PyLong_AsSsize_t(PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_INDEX));
					pyIsKey = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_KEY);
					pyData = PyTuple_GetItem(pyRowData, nColumn);
					//PyObject_ShowRepr(pyData);
					if (pyIsKey == Py_True) {
						strSql = StringAppend2(strSql, PyUnicode_AsUTF8(pyColumnName), "=? AND ");
						if (PyList_Append(pyParams, pyData) == -1) {
							return -1;
						}
					}
				}

				memset(strSql + strlen(strSql) - 5, '\0', 1); // cut off final ' AND '
				strSql = StringAppend(strSql, ";");

				MessageBoxA(0, strSql, "strSql", 0);
				if (PySequence_Size(pyParams) > 0) {
					pyCursor = PyObject_CallMethod(self->pyConnection, "execute", "(sO)", strSql, PyList_AsTuple(pyParams));
					if (pyCursor == NULL) {
						return -1;
					}
					pyCursor = PyObject_CallMethod(self->pyConnection, "commit", NULL);
					if (pyCursor == NULL) {
						return -1;
					}
					iRecordsChanged++;
				}
				else {
					PyErr_SetString(PyExc_RuntimeError, "No key columns. Can not delete.");
					return -1;
				}
				Py_DECREF(pyCursor);

				if (self->strDeleteSQL)
					PyMem_RawFree(self->strDeleteSQL);
				self->strDeleteSQL = strSql;

				Py_XDECREF(self->pyParams);
				self->pyParams = pyParams;

			}
			if (PyList_SetSlice(self->pyRows, nRow, nRow, NULL) == -1)
				return -1;
		}
		// INSERT
		else if (pyRowNew == Py_True) {
			strSql = StringAppend(NULL, (LPCSTR)"INSERT INTO ");
			strSql = StringAppend2(strSql, PyUnicode_AsUTF8(self->pyTable), (LPCSTR)" (");
			strSql2 = StringAppend(NULL, (LPCSTR)") VALUES (");

			while (PyDict_Next(self->pyColumns, &nPos, &pyColumnName, &pyColumn)) {
				if (PyErr_Occurred()) {
					PyErr_Print();
					return -1;
				}
				nColumn = PyLong_AsSsize_t(PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_INDEX));
				pyIsKey = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_KEY);
				pyData = PyTuple_GetItem(pyRowData, nColumn);

				if (pyIsKey != Py_None && self->pyAutoColumn != pyColumn) {
					strSql = StringAppend2(strSql, PyUnicode_AsUTF8(pyColumnName), ",");
					strSql2 = StringAppend(strSql2, "?,");
					//MessageBoxA(0, strSql, "strSql", 0);
					//PyObject_ShowRepr(pyData);
					if (PyList_Append(pyParams, pyData) == -1) {
						return -1;
					}
				}
				//Py_DECREF(pyData);
				//ShowInts(L"X", pyData->ob_refcnt, pyRowData->ob_refcnt);
				//MessageBoxA(0, strSql, "strSql", 0);
			}

			if (PySequence_Size(pyParams) == 0) {
				PyErr_SetString(PyExc_RuntimeError, "No columns.");
				return -1;
			}

			memset(strSql + strlen(strSql) - 1, '\0', 1); // cut off final comma
			memset(strSql2 + strlen(strSql2) - 1, '\0', 1);
			strSql = StringAppend2(strSql, strSql2, ");");

			MessageBoxA(0, strSql, "Final", 0);
			XX(pyParams);
			pyCursor = PyObject_CallMethod(self->pyConnection, "execute", "(sO)", strSql, PyList_AsTuple(pyParams));
			if (pyCursor == NULL) {
				PyErr_Print();
				return -1;
			}

			if (self->pyAutoColumn) {
				pyLastRowID = PyObject_CallMethod(pyCursor, "lastrowid", NULL);
				if (pyLastRowID == Py_None) {
					self->iLastRowID = -1;
					Py_XDECREF(pyLastRowID);
				}
				else {
					nColumn = PyLong_AsSsize_t(PyStructSequence_GetItem(self->pyAutoColumn, PXDYNASETCOLUMN_INDEX));
					tmp = PyTuple_GetItem(pyRowData, nColumn);
					PyTuple_SET_ITEM(pyRowData, nColumn, pyLastRowID);
					Py_XDECREF(tmp);
				}
			}

			pyCursor = PyObject_CallMethod(self->pyConnection, "commit", NULL);
			if (pyCursor == NULL) {
				return -1;
			}
			iRecordsChanged++;
			Py_DECREF(pyCursor);

			if (self->strInsertSQL)
				PyMem_RawFree(self->strInsertSQL);
			self->strInsertSQL = strSql;
			PyMem_RawFree(strSql2);

			Py_XDECREF(self->pyParams);
			self->pyParams = pyParams;
			Py_DECREF(pyRowNew); // Py_True
			Py_INCREF(Py_False);
			PyStructSequence_SetItem(pyRow, PXDYNASETROW_NEW, Py_False);
		}
		// UPDATE
		else if (pyRowDataOld != Py_None) {
			strSql = StringAppend(NULL, (LPCSTR)"UPDATE ");
			strSql = StringAppend2(strSql, PyUnicode_AsUTF8(self->pyTable), (LPCSTR)" SET ");
			strSql2 = StringAppend(NULL, (LPCSTR)" WHERE ");

			while (PyDict_Next(self->pyColumns, &nPos, &pyColumnName, &pyColumn)) {
				//PyObject_ShowRepr(PyErr_Occurred());
				if (PyErr_Occurred()) {
					PyErr_Print();
					return -1;
				}
				//PyObject_ShowRepr(self->pyColumns);
				nColumn = PyLong_AsSsize_t(PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_INDEX));
				pyIsKey = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_KEY);
				pyData = PyTuple_GetItem(pyRowData, nColumn);
				//PyObject_ShowRepr(pyData);
				if (pyIsKey == Py_False) {
					strSql = StringAppend2(strSql, PyUnicode_AsUTF8(pyColumnName), "=?,");
					if (PyList_Append(pyParams, pyData) == -1) {
						return -1;
					}
					//PyObject_ShowRepr(pyParams);
				}
				else if (pyIsKey == Py_True) {
					strSql2 = StringAppend2(strSql, PyUnicode_AsUTF8(pyColumnName), "=? AND ");
					if (PyList_Append(pyParams, pyData) == -1) {
						return -1;
					}
				}
			}

			if (PySequence_Size(pyParams) == 0) {
				PyErr_SetString(PyExc_RuntimeError, "No key columns given. Can not update.");
				return -1;
			}

			memset(strSql + strlen(strSql) - 1, '\0', 1); // cut off final comma
			memset(strSql2 + strlen(strSql2) - 5, '\0', 1); // cut off final ' AND '
			strSql = StringAppend2(strSql, strSql2, ";");

			MessageBoxA(0, strSql, "strSql", 0);
			PyObject_ShowRepr(pyParams);

			//pyArgs = Py_BuildValue("(sO)", strSql, PyList_AsTuple(pyParams)); // pyParams is PyList
			pyCursor = PyObject_CallMethod(self->pyConnection, "execute", "(sO)", strSql, PyList_AsTuple(pyParams));
			PyErr_Print();
			if (pyCursor == NULL) {
				return -1;
			}
			pyCursor = PyObject_CallMethod(self->pyConnection, "commit", NULL);
			if (pyCursor == NULL) {
				return -1;
			}
			iRecordsChanged++;

			Py_DECREF(pyCursor);

			if (self->strUpdateSQL)
				PyMem_RawFree(self->strUpdateSQL);
			self->strUpdateSQL = strSql;
			PyMem_RawFree(strSql2);

			Py_XDECREF(self->pyParams);
			self->pyParams = pyParams;
			Py_DECREF(pyRowDataOld);
			Py_INCREF(Py_None);
			PyStructSequence_SetItem(pyRow, PXDYNASETROW_DATAOLD, Py_None);
		}
		Py_XDECREF(pyParams);
	}
	self->bDirty = FALSE;
	self->bLocked = TRUE;

	MessageBox(NULL, L"Saved DS!", L"congr", MB_ICONERROR);

	if (iRecordsChanged > 0 && !PxDynaset_DataChanged(self, -1, NULL))
		return -1;
	return iRecordsChanged;
}

BOOL
PxDynaset_NewRow(PxDynasetObject* self, Py_ssize_t nRow)
{
	PyObject* pyColumnName, *pyColumn, *pyRow, *pyRowData, *pyFreshRowData, *pyData, *pyIndex;
	Py_ssize_t nPos = 0, nCol = 0, nAutoCol = -1;

	if (self->pyEmptyRowData == NULL) {
		pyRowData = PyList_New(0);

		if (self->pyAutoColumn) {
			pyColumn = PyStructSequence_GetItem(self->pyAutoColumn, PXDYNASETCOLUMN_INDEX);
			if (pyColumn == Py_None) { // query has not been run yet
				nAutoCol = 0;
				PyStructSequence_SetItem(pyColumn, PXDYNASETCOLUMN_INDEX, PyLong_FromSsize_t(0));
			}
			else
				nAutoCol = PyLong_AsSsize_t(pyColumn);
		}

		while (PyDict_Next(self->pyColumns, &nPos, &pyColumnName, &pyColumn)) {
			if (PyErr_Occurred()) {
				PyErr_Print();
				return FALSE;
			}
			//ShowInts(L"X", nCol, nAutoCol);

			if (nCol == nAutoCol)
				pyData = PyLong_FromLong(-1);
			else {
				pyData = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_DEFAULT);
				if (pyData == Py_None) {
					pyData = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_DEFFUNC);
					if (pyData != Py_None) {
						if (!(pyData = PyObject_CallObject(pyData, NULL)))
							return FALSE;
					}
				}
				Py_INCREF(pyData);
			}
			PyList_Append(pyRowData, pyData);
			//PyList_Append(pyRowData, Py_None);
			pyIndex = PyStructSequence_GetItem(pyColumn, PXDYNASETCOLUMN_INDEX);
			if (pyIndex == Py_None)
				PyStructSequence_SetItem(pyColumn, PXDYNASETCOLUMN_INDEX, PyLong_FromSsize_t(nCol));
			nCol++;
		}
		//PyObject_ShowRepr(pyRowData);
		self->pyEmptyRowData = PyList_AsTuple(pyRowData);
		Py_DECREF(pyRowData);
		//PyObject_ShowRepr(self->pyEmptyRowData);
	}

	nPos = PyTuple_Size(self->pyEmptyRowData);
	pyFreshRowData = PyTuple_GetSlice(self->pyEmptyRowData, 0, nPos); // make a copy
	//PyObject_ShowRepr(pyFreshRowData);
	Py_DECREF(pyFreshRowData); // for some reason ref count is already 2
	//ShowInts(L"f", 0, pyFreshRowData->ob_refcnt);
	pyRow = PyStructSequence_New(&PxDynasetRowType);
	Py_INCREF(Py_None);
	Py_INCREF(Py_True);
	Py_INCREF(Py_False);
	PyStructSequence_SetItem(pyRow, PXDYNASETROW_DATA, pyFreshRowData);
	PyStructSequence_SetItem(pyRow, PXDYNASETROW_DATAOLD, Py_None);
	PyStructSequence_SetItem(pyRow, PXDYNASETROW_NEW, Py_True);
	PyStructSequence_SetItem(pyRow, PXDYNASETROW_DELETE, Py_False);

	//PyObject_ShowRepr(pyRow);
	int iResult;
	if (nRow == -1)
		iResult = PyList_Append(self->pyRows, pyRow);
	else
		iResult = PyList_Insert(self->pyRows, nRow + 1, pyRow);

	if (iResult == -1) {
		//PyErr_SetString(PyExc_RuntimeError, "Can not add row to Dynaset.");
		Py_DECREF(pyFreshRowData);
		return FALSE;
	}
	//Py_DECREF(pyItem);
	self->nRows++;
	//PyObject_ShowRepr(self->pyRows);
	if (!PxDynaset_DataChanged(self, -1, NULL))
		return FALSE;
	return TRUE;
}


static PyObject*
PxDynaset_new_row(PxDynasetObject* self, PyObject *args, PyObject *kwds)
{
	static char *kwlist[] = { "row", NULL };
	Py_ssize_t nRow = self->nRow;
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|n", kwlist, &nRow)) {
		return NULL;
	}
	if (nRow == -1)
		nRow = self->nRows - 1;

	if (!PxDynaset_SetRow(self, -1))
		return NULL;
	if (!PxDynaset_NewRow(self, nRow))
		return NULL;
	// prepopulate key columns from parent
	if (!PxDynaset_SetRow(self, nRow))
		return NULL;
	Py_RETURN_NONE;
}

BOOL
PxDynaset_Undo(PxDynasetObject* self, Py_ssize_t nRow)
{
	PyObject* pyRowDataOld;
	PyObject* pyRow = PyList_GetItem(self->pyRows, nRow);

	if (pyRowDataOld = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATAOLD) == Py_None) { // old data
		PyErr_SetString(PyExc_RuntimeError, "Row is still clean.");
		return FALSE;
	}
	PyObject* pyRowData = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATA);
	Py_DECREF(pyRowData);

	PyStructSequence_SetItem(pyRow, PXDYNASETROW_DATA, pyRowDataOld);
	PyStructSequence_SetItem(pyRow, PXDYNASETROW_DATAOLD, Py_None);
	return TRUE;
}

BOOL
PxDynaset_DeleteRow(PxDynasetObject* self, Py_ssize_t nRow)
{
	PyObject* pyRow = PyList_GetItem(self->pyRows, nRow);
	PyObject* pyDelete = PyStructSequence_GetItem(pyRow, PXDYNASETROW_DELETE);
	if (pyDelete == Py_True)
		return TRUE;
	Py_DECREF(pyDelete);
	PyStructSequence_SetItem(pyRow, PXDYNASETROW_DELETE, Py_True);
	Py_INCREF(Py_True);
	if (!PxDynaset_DataChanged(self, -1, NULL))
		return FALSE;
	return TRUE;
}

BOOL PxDynaset_AddWidget(PxDynasetObject* self, PxWidgetObject *pyWidget)
{
	if (PyList_Append(self->pyWidgets, (PyObject*)pyWidget) == -1) {
		//PyErr_SetString(PyExc_RuntimeError, "Can not append widget to Dynaset.");
		return FALSE;
	}
	//Py_INCREF(pyWidget); not necessary
	return TRUE;
}

BOOL PxDynaset_RemoveWidget(PxDynasetObject* self, PxWidgetObject *pyWidget)
{
	if (PySequence_DelItem((PyObject*)self->pyWidgets, PySequence_Index(self->pyWidgets, (PyObject*)pyWidget)) == -1) {
		//PyErr_SetString(PyExc_RuntimeError, "Can not remove widget from Dynaset.");
		return FALSE;
	}
	return TRUE;
}

static PyObject*
PxDynaset_edit(PxDynasetObject* self, PyObject *args)
{
	if (!PxDynaset_SetFlagAndPropagateDown(self, offsetof(PxDynasetObject, bLocked), FALSE))
		return NULL;
	Py_RETURN_NONE;
}

static PyObject*
PxDynaset_save(PxDynasetObject* self, PyObject *args)
{
	if (PxDynaset_Save(self) == -1)
		return NULL;
	Py_RETURN_NONE;
}

static PyObject*
PxDynaset_undo(PxDynasetObject* self, PyObject *args)
{
	//MessageBox(NULL, L"Undo!", L"Error", MB_ICONERROR);
	if (self->nRow == -1) {
		PyErr_SetString(PyExc_RuntimeError, "No row selected.");
		return NULL;
	}
	if (PxDynaset_Undo(self, self->nRow))
		Py_RETURN_NONE;
	else
		return NULL;
}

static PyObject*
PxDynaset_delete(PxDynasetObject* self, PyObject *args)
{
	//MessageBox(NULL, L"Delete!", L"Error", MB_ICONERROR);
	if (self->nRow == -1) {
		PyErr_SetString(PyExc_RuntimeError, "No row selected.");
		return NULL;
	}
	if (!PxDynaset_DeleteRow(self, self->nRow))
		return NULL;
	Py_RETURN_NONE;
}

BOOL
PxDynaset_ParentSelectionChanged(PxDynasetObject* self)
{
	PyObject* pyRes = NULL;
	if (self->pyOnParentSelectionChangedCB) {
		PyObject* pyArgs = PyTuple_Pack(1, (PyObject*)self);
		pyRes = PyObject_CallObject(self->pyOnParentSelectionChangedCB, pyArgs);
		Py_XDECREF(pyArgs);
		if (pyRes == NULL)
			return FALSE;
		else {
			Py_XDECREF(pyRes);
		}
	}
	//MessageBox(NULL, L"PxDynaset_ParentSelectionChanged!", L"Error", MB_ICONERROR);

	if (self->bAutoExecute) {
		if (self->pyParent->nRow == -1)
			return PxDynaset_Clear(self);
		else
			if ((pyRes = PxDynaset_execute(self, NULL, NULL)) == NULL)
				return FALSE;
			else
				Py_XDECREF(pyRes);
	}
	return TRUE;
}

BOOL
PxDynaset_Freeze(PxDynasetObject* self)
{
	if (self->bFrozen)
		return TRUE;
	self->bFrozen = TRUE;

	if (self->pyParent) {
		PxDynaset_Freeze(self->pyParent);
		/* freeze selection widgets */
	}
	// notify all table widgets...
	return TRUE;
}

BOOL
PxDynaset_Thaw(PxDynasetObject* self)
{
	// Check from top if all clean...
	return TRUE;
}

BOOL
PxDynaset_DataChanged(PxDynasetObject* self, Py_ssize_t nRow, PyObject* pyColumn)
{
	OutputDebugString(L"\n*---- PxDynaset_DataChanged");
	// iterate over bound widgets, refresh if single data w and current row changed, pass on if table
	PyObject* pyResult = NULL;
	PxWidgetObject* pyDependent;
	Py_ssize_t n, nLen = PySequence_Size(self->pyWidgets);
	self->bBroadcasting = TRUE;
	for (n = 0; n < nLen; n++) {
		pyDependent = (PxWidgetObject*)PyList_GetItem(self->pyWidgets, n);
		OutputDebugString(L"\n*---- PxDynaset call refresh");
		//XX(pyDependent);
		if (pyDependent->bTable) {
			if (nRow == -1)
				pyResult = PyObject_CallMethod((PyObject*)pyDependent, "refresh", NULL);
			else
				pyResult = PyObject_CallMethod((PyObject*)pyDependent, "refresh_cell", "nO", nRow, pyColumn);
		}
		else if ((self->nRow == nRow && pyDependent->pyDataColumn == pyColumn) || (nRow == -1 && self->nRow != -1))
			pyResult = PyObject_CallMethod((PyObject*)pyDependent, "refresh", NULL);
		OutputDebugString(L"\n*---- PxDynaset call refreshed");
		if (pyResult == NULL)
			return FALSE;
		else
			Py_DECREF(pyResult);
	}
	self->bBroadcasting = FALSE;
	OutputDebugString(L"\n*---- PxDynaset_DataChanged out");

	return PxDynaset_UpdateControlWidgets(self);
}

static BOOL
PxDynaset_UpdateControlWidgets(PxDynasetObject* self)
{
	BOOL bDirty = FALSE, bNew = FALSE, bDelete = FALSE, bEnable = FALSE;
	if (self->nRow != -1) {
		PyObject* pyRow = PyList_GetItem(self->pyRows, self->nRow);
		//XX(pyRow);

		bDirty = (PyStructSequence_GetItem(pyRow, PXDYNASETROW_DATAOLD) != Py_None);
		bNew = (PyStructSequence_GetItem(pyRow, PXDYNASETROW_NEW) == Py_True);
		bDelete = (PyStructSequence_GetItem(pyRow, PXDYNASETROW_DELETE) == Py_True);

		if (PyErr_Occurred())
			return FALSE;
	}

	if (self->pyEditButton) {
		bEnable = (self->bLocked && self->nRow != -1);
		EnableWindow(self->pyEditButton->hWin, bEnable);
	}

	if (self->pyNewButton) {
		bEnable = !bDirty && !bNew;
		EnableWindow(self->pyNewButton->hWin, bEnable);
	}

	if (self->pyUndoButton) {
		bEnable = bDirty || bDelete;
		EnableWindow(self->pyUndoButton->hWin, bEnable);
	}

	if (self->pySaveButton) {
		bEnable = self->bDirty;// || bNew || bDelete;
		EnableWindow(self->pySaveButton->hWin, bEnable);
	}

	if (self->pyDeleteButton) {
		bEnable = !bDelete;
		EnableWindow(self->pyDeleteButton->hWin, bEnable);
	}

	if (self->pyOkButton) {
		bEnable = self->nRow != -1;
		EnableWindow(self->pyOkButton->hWin, bEnable);
	}

	return TRUE;
}

static BOOL
PxDynaset_SetFlagAndPropagateDown(PxDynasetObject* self, size_t nOffset, BOOL bFlag)
{
	PxDynasetObject* pyChild;
	Py_ssize_t n, nLen;
	nLen = PySequence_Size(self->pyChildren);

	*(BOOL*)(self + nOffset) = bFlag;
	self->bLocked = FALSE;
	if (!PxDynaset_DataChanged(self, -1, NULL))
		return FALSE;
	if (self->bLocked)
		MessageBox(NULL, L"bLocked!", L"Error", MB_ICONERROR);
	for (n = 0; n < nLen; n++) {
		pyChild = (PxDynasetObject*)PyList_GetItem(self->pyChildren, n);
		if (!PxDynaset_SetFlagAndPropagateDown(pyChild, nOffset, bFlag))
			return FALSE;
	}
	return TRUE;
}

BOOL
PxDynaset_SetRow(PxDynasetObject* self, Py_ssize_t nRow)
{
	PxWidgetObject* pyDependent;
	PyObject* pyResult;
	Py_ssize_t n, nLen;

	if (self->nRow == nRow)
		return TRUE;

	if (FALSE && self->bFrozen) {
		PyErr_SetString(PyExc_RuntimeError, "Dynaset frozen.");
		return FALSE;
	}

	if (nRow < -1 || nRow > self->nRows) {
		PyErr_SetString(PyExc_IndexError, "Row number out of range.");
		return FALSE;
	}
	self->nRow = nRow;

	if (!PxDynaset_DataChanged(self, -1, NULL))
		return FALSE;

	// notify bound row pointing and non-table widgets
	nLen = PySequence_Size(self->pyWidgets);
	for (n = 0; n < nLen; n++) {
		pyDependent = (PxWidgetObject*)PyList_GetItem(self->pyWidgets, n);
		if (pyDependent->bPointer)
			pyResult = PyObject_CallMethod((PyObject*)pyDependent, "refresh_row_pointer", NULL);
		else if (!pyDependent->bTable)
			pyResult = PyObject_CallMethod((PyObject*)pyDependent, "refresh", NULL);
		else break;
		if (pyResult == NULL)
			return FALSE;
		else
			Py_DECREF(pyResult);
	}

	// notify child Dynasets
	nLen = PySequence_Size(self->pyChildren);
	for (n = 0; n < nLen; n++) {
		pyDependent = (PxDynasetObject*)PyList_GetItem(self->pyChildren, n);
		if (!PxDynaset_ParentSelectionChanged(pyDependent))
			return FALSE;
	}
	return TRUE;
}

static PyMethodDef PxDynaset_ControlButtons[] = {
	{ "new", (PyCFunction)PxDynaset_new_row, METH_VARARGS, "New button pressed" },
	{ "edit", (PyCFunction)PxDynaset_edit, METH_VARARGS, "Edit button pressed" },
	{ "undo", (PyCFunction)PxDynaset_undo, METH_VARARGS, "Undo button pressed" },
	{ "save", (PyCFunction)PxDynaset_save, METH_VARARGS, "Save button pressed" },
	{ "delete", (PyCFunction)PxDynaset_delete, METH_VARARGS, "Delete button pressed" },
};

static int
PxDynaset_setattro(PxDynasetObject* self, PyObject* pyAttributeName, PyObject *pyValue)
{
	if (PyUnicode_Check(pyAttributeName)) {
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "autoColumn") == 0) {

			if (!PyObject_TypeCheck(pyValue, &PxDynasetColumnType)) {
				PyErr_SetString(PyExc_TypeError, "'autoColumn' must be a DataColumn.");
				return -1;
			}

			PyObject* pyAttr = PyStructSequence_GetItem(pyValue, PXDYNASETCOLUMN_TYPE);
			if (pyAttr != &PyLong_Type) {
				PyErr_SetString(PyExc_TypeError, "'autoColumn.type' must be 'int'.");
				return -1;
			}

			pyAttr = PyStructSequence_GetItem(pyValue, PXDYNASETCOLUMN_KEY);
			if (pyAttr != Py_True) {
				Py_DECREF(pyAttr);
				PyStructSequence_SetItem(pyValue, PXDYNASETCOLUMN_KEY, Py_True);
				Py_INCREF(Py_True);
			}

			Py_XDECREF(self->pyAutoColumn);
			self->pyAutoColumn = pyValue;
			Py_INCREF(self->pyAutoColumn);
			return 0;
		}

		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "row") == 0) {
			return PxDynaset_SetRow(self, PyLong_AsSsize_t(pyValue)) ? 0 : -1;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "buttonNew") == 0) {
			PxButtonObject* tmp = self->pyNewButton;
			Py_INCREF(pyValue);
			self->pyNewButton = (PxButtonObject*)pyValue;
			Py_XDECREF(tmp);
			self->pyNewButton->pyOnClickCB = PyCFunction_NewEx(&PxDynaset_ControlButtons[0], (PyObject *)self, NULL);
			if (self->pyNewButton->pyOnClickCB == NULL) {
				Py_DECREF(pyValue);
				return -1;
			}
			return 0;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "buttonEdit") == 0) {
			PxButtonObject* tmp = self->pyEditButton;
			Py_INCREF(pyValue);
			self->pyEditButton = (PxButtonObject*)pyValue;
			Py_XDECREF(tmp);
			self->pyEditButton->pyOnClickCB = PyCFunction_NewEx(&PxDynaset_ControlButtons[1], (PyObject *)self, NULL);
			if (self->pyEditButton->pyOnClickCB == NULL) {
				Py_DECREF(pyValue);
				return -1;
			}
			return 0;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "buttonUndo") == 0) {
			PxButtonObject* tmp = self->pyUndoButton;
			Py_INCREF(pyValue);
			self->pyUndoButton = (PxButtonObject*)pyValue;
			Py_XDECREF(tmp);
			self->pyUndoButton->pyOnClickCB = PyCFunction_NewEx(&PxDynaset_ControlButtons[2], (PyObject *)self, NULL);
			if (self->pyUndoButton->pyOnClickCB == NULL) {
				Py_DECREF(pyValue);
				return -1;
			}
			return 0;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "buttonSave") == 0) {
			PxButtonObject* tmp = self->pySaveButton;
			Py_INCREF(pyValue);
			self->pySaveButton = (PxButtonObject*)pyValue;
			Py_XDECREF(tmp);
			self->pySaveButton->pyOnClickCB = PyCFunction_NewEx(&PxDynaset_ControlButtons[3], (PyObject *)self, NULL);
			if (self->pySaveButton->pyOnClickCB == NULL) {
				Py_DECREF(pyValue);
				return -1;
			}
			return 0;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "buttonDelete") == 0) {
			PxButtonObject* tmp = self->pyDeleteButton;
			Py_INCREF(pyValue);
			self->pyDeleteButton = (PxButtonObject*)pyValue;
			Py_XDECREF(tmp);
			self->pyDeleteButton->pyOnClickCB = PyCFunction_NewEx(&PxDynaset_ControlButtons[4], (PyObject *)self, NULL);
			if (self->pyDeleteButton->pyOnClickCB == NULL) {
				Py_DECREF(pyValue);
				return -1;
			}
			return 0;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "on_parent_changed") == 0) {
			if (PyCallable_Check(pyValue)) {
				Py_XINCREF(pyValue);
				Py_XDECREF(self->pyOnParentSelectionChangedCB);
				self->pyOnParentSelectionChangedCB = pyValue;
				return 0;
			}
			else {
				PyErr_SetString(PyExc_TypeError, "Assigned object must be callable.");
				return -1;
			}
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "before_save") == 0) {
			if (PyCallable_Check(pyValue)) {
				Py_XINCREF(pyValue);
				Py_XDECREF(self->pyBeforeSaveCB);
				self->pyBeforeSaveCB = pyValue;
				return 0;
			}
			else {
				PyErr_SetString(PyExc_TypeError, "Assigned object must be callable.");
				return -1;
			}
		}
	}
	return PyObject_GenericSetAttr((PyObject*)self, pyAttributeName, pyValue);
}

static PyObject *
PxDynaset_getattro(PxDynasetObject* self, PyObject* pyAttributeName)
{
	PyObject* pyResult;
	pyResult = PyObject_GenericGetAttr((PyObject *)self, pyAttributeName);
	if (pyResult == NULL && PyErr_ExceptionMatches(PyExc_AttributeError) && PyUnicode_Check(pyAttributeName)) {
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "parent") == 0) {
			PyErr_Clear();
			if (self->pyParent) {
				Py_INCREF(self->pyParent);
				return (PyObject*)self->pyParent;
			}
			else
				Py_RETURN_NONE;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "lastInsertSQL") == 0) {
			PyErr_Clear();
			if (self->strInsertSQL)
				return PyUnicode_FromString(self->strInsertSQL);
			else
				Py_RETURN_NONE;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "lastUpdateSQL") == 0) {
			PyErr_Clear();
			if (self->strUpdateSQL)
				return PyUnicode_FromString(self->strUpdateSQL);
			else
				Py_RETURN_NONE;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "lastDeleteSQL") == 0) {
			PyErr_Clear();
			if (self->strDeleteSQL)
				return PyUnicode_FromString(self->strDeleteSQL);
			else
				Py_RETURN_NONE;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "lastSQLParameters") == 0) {
			PyErr_Clear();
			if (self->pyParams) {
				Py_INCREF(self->pyParams);
				return self->pyParams;
			}
			else
				Py_RETURN_NONE;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "autoColumn") == 0) {
			PyErr_Clear();
			if (self->pyAutoColumn) {
				Py_INCREF(self->pyAutoColumn);
				return self->pyAutoColumn;
			}
			else
				Py_RETURN_NONE;
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "lastRowID") == 0) {
			PyErr_Clear();
			if (self->iLastRowID > -1) {
				return PyLong_FromLong(self->iLastRowID);
			}
			else
				Py_RETURN_NONE;
		}

		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "row") == 0) {
			PyErr_Clear();
			if (self->nRow == -1)
				Py_RETURN_NONE;
			else
				return PyLong_FromSsize_t(self->nRow);
		}
		if (PyUnicode_CompareWithASCIIString(pyAttributeName, "buttonSave") == 0) {
			PyErr_Clear();
			if (self->pySaveButton) {
				Py_INCREF(self->pySaveButton);
				return (PyObject *)self->pySaveButton;
			}
			else
				Py_RETURN_NONE;
		}
	}
	return pyResult;
}

static PyObject* // new ref
PxDynaset_str(PxDynasetObject* self)
{
	return PyUnicode_FromFormat("pylax.Dynaset object on table '%.50s'", PyUnicode_AsUTF8(self->pyTable));
}

static void
PxDynaset_dealloc(PxDynasetObject* self)
{
	Py_XDECREF(self->pyParent);
	Py_XDECREF(self->pyConnection);
	Py_XDECREF(self->pyTable);
	Py_XDECREF(self->pyCursor);
	Py_XDECREF(self->pyColumns);
	Py_XDECREF(self->pyAutoColumn);
	Py_XDECREF(self->pyRows);
	Py_XDECREF(self->pyChildren);
	Py_XDECREF(self->pyEmptyRowData);
	Py_XDECREF(self->pyQuery);
	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyMemberDef PxDynaset_members[] = {
	{ "rows", T_PYSSIZET, offsetof(PxDynasetObject, nRows), READONLY, "Row count. -1 if still not executed." },
	{ "query", T_OBJECT, offsetof(PxDynasetObject, pyQuery), 0, "Query string" },
	{ "autoExecute", T_BOOL, offsetof(PxDynasetObject, bAutoExecute), 0, "Execute query if parent row has changed." },
	{ "buttonOK", T_OBJECT, offsetof(PxDynasetObject, pyOkButton), 0, "Close the dialog." },
	{ "buttonSearch", T_OBJECT, offsetof(PxDynasetObject, pySearchButton), 0, "Execute seach." },
	{ "dirty", T_BOOL, offsetof(PxDynasetObject, bDirty), READONLY, "True if there are unwritten changes." },
	{ "frozen", T_BOOL, offsetof(PxDynasetObject, bFrozen), READONLY, "If True row pointer can not be moved (dirty child records exist)." },
	{ "frozenIfDirty", T_BOOL, offsetof(PxDynasetObject, bFrozenIfDirty), READONLY, "If True user can not navigate away from record before saving." },
	{ NULL }  /* Sentinel */
};

static PyMethodDef PxDynaset_methods[] = {
	{ "add_column", (PyCFunction)PxDynaset_add_column, METH_VARARGS | METH_KEYWORDS, "Add a data column" },
	{ "get_column", (PyCFunction)PxDynaset_get_column, METH_VARARGS, "Returns a data column as named tuple." },
	{ "execute", (PyCFunction)PxDynaset_execute, METH_VARARGS | METH_KEYWORDS, "Run the query" },
	{ "get_row", (PyCFunction)PxDynaset_get_row, METH_VARARGS, "Returns a data row as named tuple." },
	{ "get_data", (PyCFunction)PxDynaset_get_data, METH_VARARGS, "Returns the data for a row/column combination" },
	{ "set_data", (PyCFunction)PxDynaset_set_data, METH_VARARGS, "Sets the data for a row/column combination" },
	{ "get_row_data", (PyCFunction)PxDynaset_get_row_data, METH_VARARGS, "Returns a data row as named tuple." },
	{ "clear", (PyCFunction)PxDynaset_clear, METH_NOARGS, "Empties the data." },
	{ "save", (PyCFunction)PxDynaset_save, METH_NOARGS, "Save the data." },
	{ NULL }  /* Sentinel */
};

PyTypeObject PxDynasetType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"pylax.Dynaset",           /* tp_name */
	sizeof(PxDynasetObject),   /* tp_basicsize */
	0,                         /* tp_itemsize */
	(destructor)PxDynaset_dealloc, /* tp_dealloc */
	0,                         /* tp_print */
	0,                         /* tp_getattr */
	0,                         /* tp_setattr */
	0,                         /* tp_reserved */
	PxDynaset_str,             /* tp_repr */
	0,                         /* tp_as_number */
	0,                         /* tp_as_sequence */
	0,                         /* tp_as_mapping */
	0,                         /* tp_hash  */
	0,                         /* tp_call */
	PxDynaset_str,             /* tp_str */
	PxDynaset_getattro,        /* tp_getattro */
	PxDynaset_setattro,        /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,        /* tp_flags */
	"The result of a database query that can be manipulated and updates back", /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	PxDynaset_methods,         /* tp_methods */
	PxDynaset_members,         /* tp_members */
	0,                         /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	(initproc)PxDynaset_init,  /* tp_init */
	0,                         /* tp_alloc */
	PxDynaset_new,             /* tp_new */
};

PyTypeObject PxDynasetColumnType = { 0, 0, 0, 0, 0, 0 };
PyTypeObject PxDynasetRowType = { 0, 0, 0, 0, 0, 0 };
