/*
 * Python wrapper for reading BUFR files incrementally 
 *
 * Author Kristian Rune Larsen <krl@dmi.dk>
 *
 *
 *
 */


#include <stdio.h> 
#include <stdlib.h>
#include "Python.h"
#include "numpy/arrayobject.h"
/*#include "numarray/arrayobject.h"*/
#include "structmember.h"


/* BUFR fill values for int and double precision*/

#define IFILL 2147483647
#define DFILL 1.7e38

// Number of values and elements in the BUFR file. 
#define KVALS 360000
#define KELEM 40000


static int my_ref = 0;

/* Extern fortran call*/
extern int readbufr(FILE *, char *, int *); 
extern void bus012_(int*, int *, int *, int * , int *, int * , int *);
extern void bufrex_(int *, 
                    long *, 
                    int * , 
                    int * , 
                    int * , 
                    int *, 
                    int * , 
                    int *,
                    int *, 
                    char *, 
                    char * , 
                    int *, 
                    double *, 
                    char *, 
                    int *);

/* Helper functions */
int trim( char* str ) {

	/* 
	 * Get rid of trailing white-space characters, return number of removed characters
	 */

	char *p;
	int n = 0;
	size_t l = strlen( str );

	if ( l > 0 )
		for ( p = &str[l-1]; n < l && isspace( (int)*p ); --p, ++n )
			*p = '\0';
	return n;
};

void subst_space(char *str) {
	/* Creates a NC identifier from the bufr entry name by substituting
	   disallowed characters  with underscore. */

	int n = 0;
	while(str[n] != '\0'){
		if (str[n]  == ' ' || 
				str[n] == ')'  || 
				str[n] == '('  || 
				str[n] == '/'  || 
				str[n] == '*'  ||
				str[n] == '"'  ||
				str[n] == '\\')
			str[n] = '_';
		n++;
	}
}

int find_entry_type(double * data , int len) {
	/* Figure out type double or int */

	/* integer assumed if all values a below 100 times machine epsilon
	   ...epsilon is approx 1e-7 for single point precision (float) ... */

	int i;
	for(i=0;i<len;i++)
		if( (data[i] - floor(data[i])) > 10e-7)
			return PyArray_DOUBLE;
	return PyArray_INT;
}

int collapse(double * data , int count ) {
    /* Returns true if all data is identical */
    double val = data[0];
    int i;
    for(i=0; i< count; i++){

        if ( data[i] != val)
            return 0;
    }
    /* The values are all alike */
   return 1; 
}


/*
 * BUFR File Exception Object
 * */

static PyObject * BUFRFile_BUFRFileError;


/*
 * BUFR File entry models BUFR data subset 
 * */


typedef struct {
	/*
	 * Struct for holding data for each bufr element (scanline)
	 */
	PyObject_HEAD
	PyObject* name;     /* name */
	PyObject* unit;     /* unit */
	PyObject * data;  /* data array */

} BUFRFile_BUFRFileEntryObject;

static PyMemberDef BUFRFileEntry_Members[] = {
    {"name", T_OBJECT_EX, offsetof(BUFRFile_BUFRFileEntryObject, name), 0, "name"},
    {"unit", T_OBJECT_EX, offsetof(BUFRFile_BUFRFileEntryObject, unit), 0, "unit"},
    {"data", T_OBJECT_EX, offsetof(BUFRFile_BUFRFileEntryObject, data), 0, "data"},
    /* Sentinel */
};


static PyObject *BUFRFileEntry_new(PyTypeObject *type, PyObject *args, PyObject *kw) {
	/* Creates a new instance for a BUFRFile.BUFRFile object */
	BUFRFile_BUFRFileEntryObject *self = (BUFRFile_BUFRFileEntryObject *) type->tp_alloc(type, 0);
	self->name = NULL;
	self->unit = NULL;
	self->data = NULL;

	return (PyObject *) self;
}


static void BUFRFileEntry_dealloc(BUFRFile_BUFRFileEntryObject *self) {
    my_ref--;
	Py_XDECREF(self->data); 
	Py_XDECREF(self->name);
	Py_XDECREF(self->unit);
	self->ob_type->tp_free(self);
}

static int BUFRFileEntry_init(BUFRFile_BUFRFileEntryObject *self, PyObject *args, PyObject *kw) {

	PyObject * data;
	PyObject * name;
	PyObject * unit;

	if (!PyArg_ParseTuple(args, "OOO",&name, &unit, &data)) {
		return -1;
	}
	self->data = data;
	self->name = name;
	self->unit = unit;

    my_ref++;

	Py_XINCREF(self->data);
	Py_XINCREF(self->unit);
	Py_XINCREF(self->name);

	return 0;
}


static PyMethodDef BUFRFileEntry_Methods[] = {
	{NULL, NULL, 0, NULL },
};


static PyTypeObject BUFRFile_BUFRFileEntryType = {
		PyObject_HEAD_INIT(NULL)
			0,
		"BUFRFile.BUFRFileEntry",                /* char *tp_name; */
		sizeof(BUFRFile_BUFRFileEntryObject),          /* int tp_basicsize; */
		0,                        /* int tp_itemsize;*/
		(destructor) BUFRFileEntry_dealloc,         /* destructor tp_dealloc; */
		0,                        /* printfunc  tp_print;   */
		0,                        /* getattrfunc  tp_getattr; */
		0,                        /* setattrfunc  tp_setattr;  */
		0,                        /* cmpfunc  tp_compare;  */
		0,                        /* reprfunc  tp_repr;    */
		0,                        /* tp_as_number */
		0,                        /* PySequenceMethods *tp_as_sequence; */
		0,                        /* PyMappingMethods *tp_as_mapping; */
		0,                        /* hashfunc tp_hash;    */
		0,                        /* ternaryfunc tp_call;  */
		0,                        /* reprfunc tp_str;     */
		0,                        /* tp_getattro  */
		0,                        /* tp_setattro  */
		0,                        /* tp_as_buffer */
		Py_TPFLAGS_DEFAULT,      /* tp_flags */
		"BUFR file entry models BUFR data subsets ", /* tp_doc*/
		0,                        /* tp_traverse */
		0,                        /* tp_clear */
		0,                        /* tp_richcompare*/
		0,                        /* tp_weaklistoffset*/
		0,                        /* tp_iter*/
		0,                        /* tp_iternext */
		BUFRFileEntry_Methods,    /* tp_methods */
		BUFRFileEntry_Members,    /* tp_members */
		0,                        /* tp_getset*/
		0,                        /* tp_base */
		0,                        /* tp_dict */
		0,                        /* tp_descr_get */
		0,                        /* tp_descr_set */
		0,                        /* tp_dictoffset */
		(initproc) BUFRFileEntry_init,/* tp_init */
		0,                        /* tp_allow */
		BUFRFileEntry_new,        /* tp_new*/
		0,                        /* tp_free*/
	};


/*  
 * BUFRFile implementation below 
 *  */

typedef struct {
	PyObject_HEAD
    FILE * inpfp;
    PyObject * filename; 

	/* internal variables for reading BUFR subsection */
	char *bufr_message0;
	int *ksup, *ksec0, *ksec1, *ksec2, *ksec3, *ksec4, *key, *ktdlst,
	    *ktdexp;
	char *cnames, *cunits, *cvals;
	double *values;
	int Nbpw;
	int kelem, kvals, icode; /*number of elements, values, */

} BUFRFile_BUFRFileObject;


static PyObject *BUFRFile_new(PyTypeObject *type, PyObject *args, PyObject *kw) {
	/* Creates a new instance for a BUFRFile.BUFRFile object */
	BUFRFile_BUFRFileObject *self = (BUFRFile_BUFRFileObject *) type->tp_alloc(type, 0);
	self->inpfp = NULL;

	self->bufr_message0 = PyMem_New(char,15000);
	self->ksup =  PyMem_New(int, 9);
	self->ksec0 = PyMem_New(int,3);
	self->ksec1 = PyMem_New(int,40);
	self->ksec2 = PyMem_New(int, 4096);
	self->ksec3 = PyMem_New(int,4);
	self->ksec4 = PyMem_New(int,2);
	self->key = PyMem_New(int, 46);
	self->ktdlst = PyMem_New(int,KELEM);
	self->ktdexp = PyMem_New(int, KELEM);
	/* variable values */
	self->values = PyMem_New(double,KVALS);
	/*cnames , variable names */
	self->cnames = PyMem_New(char,KELEM*64);
	/*cunits , variable units */
	self->cunits = PyMem_New(char,KELEM*24);
	/*cvals , charater values ... not suppoted yet */
	self->cvals = PyMem_New(char,KVALS*80); 

	return (PyObject *) self;
}

static void BUFRFile_dealloc(BUFRFile_BUFRFileObject *self) {


    if(self->inpfp) {
		fclose(self->inpfp);
	}

	Py_XDECREF(self->filename); 

    PyMem_Free( self->bufr_message0 );
    PyMem_Free( self->ksup );
	PyMem_Free( self->ksec0 );
	PyMem_Free( self->ksec1 );
	PyMem_Free( self->ksec2 );
	PyMem_Free( self->ksec3 );
	PyMem_Free( self->ksec4 );
	PyMem_Free( self->key );
	PyMem_Free( self->ktdlst );
	PyMem_Free( self->ktdexp );
	PyMem_Free( self->values );
	PyMem_Free( self->cnames );
	PyMem_Free( self->cunits );
	PyMem_Free( self->cvals );
    
    self->ob_type->tp_free(self);
}

static int BUFRFile_init(BUFRFile_BUFRFileObject *self, PyObject *args, PyObject *kw) {
	PyObject * inp_filename;
	if (!PyArg_ParseTuple(args, "O", &inp_filename)) {
		return -1;
	}

    if (self->inpfp) {
		PyErr_SetString(BUFRFile_BUFRFileError, "__init__ already called");
		return -1;
	}
	if(sizeof(long) == 4) self->Nbpw=32;
	else if(sizeof(long) == 8) self->Nbpw=64;
	else{
		PyErr_SetString(BUFRFile_BUFRFileError, "Wrong architecture");
		return -1;
	}
	self->kelem = KELEM;
	self->kvals = KVALS;
	self->icode = 0;

    self->filename = inp_filename;
    Py_XINCREF(self->filename);

	self->inpfp = fopen(PyString_AsString(self->filename), "rb");
	if (self->inpfp == NULL) {
		PyErr_SetString(BUFRFile_BUFRFileError, "unable to open file ");
		return -1;
	}
	return 0;

}

static PyObject * BUFRFile_read(BUFRFile_BUFRFileObject *self) {

    if(!self->inpfp) {
		PyErr_SetString(BUFRFile_BUFRFileError, "File not open");
		return NULL;
    }

	/*********** Read in bufr messages ***********/
	int length = 15000;
	int kerr = 0;
	int * kbuff;
	int status = 0; /*status for reading one new entry */
	char* bufr_message;

	bufr_message = self->bufr_message0; /* make sure we alwas start at the
					       beginning of the message array
					       ... shouldn't be necessary. */

	/* read entry into buffer*/
	status = readbufr( self->inpfp, bufr_message, &length);


	if( status == -1 ) 
		return NULL;
	else if( status == -2 ) {
		PyErr_SetString(BUFRFile_BUFRFileError, "Error in file handling");
		return NULL;
	}   
	else if( status == -3 ) {
		PyErr_SetString(BUFRFile_BUFRFileError, "Too small input array");
		return NULL;
	}
	else if( status == -4 ) {
		PyErr_SetString(BUFRFile_BUFRFileError, "Too small input array");
		return NULL;
	}
	else if( status < 0 ) {
		PyErr_SetString(BUFRFile_BUFRFileError, "Error reading BUFR");
		return NULL;
	}
	/*    Expand bufr message calling fortran program */
	kbuff = (int *) bufr_message;
	length /= 4;

	/* Get BUFR subsection limits */
	bus012_(&length, kbuff , self->ksup, self->ksec0, self->ksec1, self->ksec2,  &kerr) ;

	if (self->ksup[5] > 1)
		self->kelem = self->kvals/self->ksup[5];
	else
		self->kelem = KELEM;

	if ( self->kelem > KELEM ) self->kelem = KELEM;
	kerr = 0;
	bufrex_(&length,
            ( long * ) kbuff,
            self->ksup,
            self->ksec0,
            self->ksec1,
            self->ksec2,
            self->ksec3,
            self->ksec4,
			&(self->kelem), 
            self->cnames, 
            self->cunits ,
            &(self->kvals),
			self->values, 
            self->cvals,
            &kerr);

	/* find number of subsets = number of meassurements */
	int nsup = self->ksup[5];

    /* since we are comming from shitty fortran we need to rotate the values , in order for it to make sense in C*/
    double * tvalues;
    tvalues = PyMem_New(double, KVALS);

    int cr,cc;
    for(cr=0; cr < self->kelem ; cr++)
        for(cc=0; cc< nsup; cc++)
            *(tvalues + cr*nsup + cc) = *(self->values + cc*self->kelem + cr);
    
    /* Copy new elements back to original array*/
    for(cr=0; cr < KVALS ; cr++)
        self->values[cr] = tvalues[cr];

    PyMem_Free(tvalues);

	if ( kerr )
	{
		PyErr_SetString(BUFRFile_BUFRFileError, "Error reading BUFR entry");
		return NULL;
	}

	int s,r;

	/*
	 * run through bufr names, units and values and insert data info
	 * bufr_entry structs. 
	 *
	 */

	char * mycname = PyMem_New(char, 65);
	char * myunits = PyMem_New(char, 25);

	/*Here we need a python list instead ... ! */
	PyObject * bufr_entries = PyList_New(0);

	char ch;

	for(s=0; s < self->kelem; s++ ){
		/* get name of this entry and trim whitespace*/
		for(r=0; r< 64; r++){
			ch = tolower(*(self->cnames + s*64 + r));
			mycname[r] = ch;
		}

		mycname[64] = '\0';
		trim(mycname);

		/* Don't accept empty names*/
		if(mycname[0] == '\0')
			continue;

		/* get name of unit of this entry and trim whitespace*/
		for(r=0; r<24; r++) {
			ch = tolower(*(self->cunits + s*24 + r));
			myunits[r] = ch;
		}
		myunits[24] = '\0'; /*insert eol missing from fortran
				      string */
		trim(myunits); /* trims white space from end of string */

		subst_space(mycname); /*make a valid name*/

		/* Create a New BUFRFileEntry object to hold the data */
		PyObject *bufr_entry_args, *bufr_entry, *name, *unit, *bdata;

		/* unit string*/
		unit = PyString_FromFormat("%s ", myunits);
		/* name string */
		name = PyString_FromFormat("%s_%d", mycname, s);

		/* subsets differ by kelem = number of elements. Notice
		 * that the fill values are from the
		 * bufr definition.
		 * mapping:
		 * missing BUFR int = 2147483647
		 * missing BUFR real = 1.7E38
		 *
		 */

		/* Create data array */
		int dimensions[1];
		dimensions[0]=nsup;
        if( collapse(self->values + s*nsup, nsup) )
            bdata = PyFloat_FromDouble( *(self->values+s*nsup) );
        else {
            bdata = PyArray_SimpleNew(1, dimensions, PyArray_DOUBLE);
            PyObject * tmp_bdata = PyArray_SimpleNewFromData(1, dimensions,PyArray_DOUBLE, (void*) (self->values + s*nsup));
            PyArray_CopyInto((PyArrayObject *) bdata, (PyArrayObject *) tmp_bdata);
            Py_XDECREF(tmp_bdata);
            /*bdata = (PyArrayObject *) PyArray_SimpleNewFromData(1, dimensions,PyArray_DOUBLE, (void*) (self->values + s*nsup));*/
        }

        if (bdata == NULL) {
            PyErr_SetString(BUFRFile_BUFRFileError, "Unable to retrieve BUFR data");
            return NULL;
        }

		/* Create bufr enty object and insert object into list */
		bufr_entry_args = PyTuple_New(3);
		PyTuple_SetItem(bufr_entry_args,0,name);
        if(unit) {
		    PyTuple_SetItem(bufr_entry_args,1, unit);
        }
        else {
            PyTuple_SetItem(bufr_entry_args,1, Py_BuildValue("s", ""));
        }
		PyTuple_SetItem(bufr_entry_args,2,bdata);

		bufr_entry =  PyObject_CallObject((PyObject *) &BUFRFile_BUFRFileEntryType, bufr_entry_args);
        Py_XDECREF( bufr_entry_args );

		if (PyList_Append(bufr_entries,bufr_entry)) {
			PyErr_SetString(BUFRFile_BUFRFileError, "Error reading BUFR entry");
			return NULL;
		}
        Py_XDECREF(bufr_entry);
	}

	PyMem_Free(mycname );
	PyMem_Free(myunits );

    /* return reference to list */
	return bufr_entries;

} 

static PyObject * BUFRFile_close(BUFRFile_BUFRFileObject *self) {
	if (! self->inpfp ) {
		PyErr_SetString(BUFRFile_BUFRFileError, "BUFRFile not open");
		return NULL;
	}
    fclose(self->inpfp);
    Py_RETURN_NONE;
}

static PyObject * BUFRFile_iter(BUFRFile_BUFRFileObject *self) {
    Py_XINCREF(self);
	return (PyObject *) self;
}
static PyObject * BUFRFile_next(BUFRFile_BUFRFileObject *self) {
    if(!self->inpfp) {
        PyErr_SetString(BUFRFile_BUFRFileError,"BUFRFile not open");
        return NULL;
    }
	PyObject * res; 
    do {
        res = BUFRFile_read(self);
        if (!res) {
            PyErr_SetString(PyExc_StopIteration, "BUFRFile not open");
            return NULL;
        }
        return res;
    }
    while(res != NULL);

    PyErr_SetString(PyExc_StopIteration, "BUFRFile not open");
    return NULL;
}

static PyObject * BUFRFile_reset(BUFRFile_BUFRFileObject *self) {
    BUFRFile_close(self);
	self->inpfp = fopen(PyString_AsString(self->filename), "rb");
    if (!self->inpfp) {
        PyErr_SetString(BUFRFile_BUFRFileError, "Unable to reset file");
        return NULL;
    }
    Py_RETURN_NONE;
}


static PyObject * BUFRFile_keys(BUFRFile_BUFRFileObject *self) {
    if(!BUFRFile_reset(self))
        return NULL;
    PyObject * bufr_entries = BUFRFile_next(self);
    if(!bufr_entries ) {
        BUFRFile_reset(self);
        return NULL;
    }
    
	PyObject * bufr_keys = PyList_New(0);
    if(!bufr_keys ) {
        PyErr_SetString(BUFRFile_BUFRFileError, "Error creating keys list");
        BUFRFile_reset(self);
        return NULL;
    }
    int i;
    for(i=0; i<PyList_Size(bufr_entries); i++) {
        BUFRFile_BUFRFileEntryObject * entry;
        entry = (BUFRFile_BUFRFileEntryObject *) PyList_GetItem(bufr_entries, i);
        if (PyList_Append(bufr_keys,entry->name)) {
            PyErr_SetString(BUFRFile_BUFRFileError, "Error getting keys");
            BUFRFile_reset(self);
            return NULL;
        }
    }
    /* Reset filepointer before returning */
    BUFRFile_reset(self);
    return bufr_keys;
}

static PyMemberDef BUFRFile_Members[] = {
    {"filename", T_OBJECT_EX, offsetof(BUFRFile_BUFRFileObject, filename), 0, "filename"},
    {NULL}  /* Sentinel */
};

static PyMethodDef BUFRFile_Methods[] = {
	{ "read", BUFRFile_read, METH_NOARGS, "Reads a subset of the BUFR file" }, 
	{ "close", BUFRFile_close, METH_NOARGS, "Closes input BUFR file"},
	{ "next",  BUFRFile_next, METH_NOARGS, "Reads next subset, for python iterator."},
	{ "keys", BUFRFile_keys, METH_NOARGS, "Returns a list of the BUFR file variables."},
	{ "reset", BUFRFile_reset, METH_NOARGS, "Resets the BUFR file pointer to initial position."},
	{NULL, NULL, 0, NULL }
};


static PyTypeObject BUFRFile_BUFRFileType = {
	PyObject_HEAD_INIT(NULL)
	0,
	"BUFRFile.BUFRFile",                /* char *tp_name; */
	sizeof(BUFRFile_BUFRFileObject),          /* int tp_basicsize; */
    0,                        /* int tp_itemsize;*/
    (destructor) BUFRFile_dealloc,         /* destructor tp_dealloc; */
    0,                        /* printfunc  tp_print;   */
    0,                        /* getattrfunc  tp_getattr; */
    0,                        /* setattrfunc  tp_setattr;  */
    0,                        /* cmpfunc  tp_compare;  */
    0,                        /* reprfunc  tp_repr;    */
    0,                        /* tp_as_number */
    0,                        /* PySequenceMethods *tp_as_sequence; */
    0,                        /* PyMappingMethods *tp_as_mapping; */
    0,                        /* hashfunc tp_hash;    */
    0,                        /* ternaryfunc tp_call;  */
    0,                        /* reprfunc tp_str;     */
    0,                        /* tp_getattro  */
    0,                        /* tp_setattro  */
    0,                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,      /* tp_flags */
    "BUFR file object supports reading of BUFR data subsets ", /* tp_doc*/
    0,                        /* tp_traverse */
    0,                        /* tp_clear */
    0,                        /* tp_richcompare*/
    0,                        /* tp_weaklistoffset*/
    BUFRFile_iter,            /* tp_iter*/
    BUFRFile_next,            /* tp_iternext */
    BUFRFile_Methods,         /* tp_methods */
    BUFRFile_Members,         /* tp_members */
    0,                        /* tp_getset*/
    0,                        /* tp_base */
    0,                        /* tp_dict */
    0,                        /* tp_descr_get */
    0,                        /* tp_descr_set */
    0,                        /* tp_dictoffset */
    (initproc) BUFRFile_init,           /* tp_init */
    0,                        /* tp_allow */
    BUFRFile_new,             /* tp_new*/
    0,                        /* tp_free*/
};


PyMODINIT_FUNC initBUFRFile() {
    import_array();
    PyObject *m;
    if (PyType_Ready(&BUFRFile_BUFRFileType) < 0 ) {
        return;
    }
    if (PyType_Ready(&BUFRFile_BUFRFileEntryType) < 0 ) {
        return;
    }
    
    m = Py_InitModule3("BUFRFile", BUFRFile_Methods, "Python BUFR file module.");
    if (m == NULL)
        return;

    BUFRFile_BUFRFileError = PyErr_NewException("BUFRFile.BUFRFileError", NULL, NULL);
    Py_INCREF(BUFRFile_BUFRFileError);
    Py_INCREF(&BUFRFile_BUFRFileType);
    Py_INCREF(&BUFRFile_BUFRFileEntryType);
    PyModule_AddObject(m, "BUFRFile", (PyObject *) &BUFRFile_BUFRFileType);
    PyModule_AddObject(m, "BUFRFileEntry", (PyObject *) &BUFRFile_BUFRFileEntryType);
    PyModule_AddObject(m, "BUFRFileError", BUFRFile_BUFRFileError);
}

