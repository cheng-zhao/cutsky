/*******************************************************************************
* write_file.h: this file is part of the cutsky program.

* cutsky: cutsky catalogue generator.

* Github repository:
        https://github.com/cheng-zhao/cutsky

* Copyright (c) 2023-2025 Cheng Zhao <zhaocheng03@gmail.com>
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.

*******************************************************************************/

#ifndef _WRITE_FILE_H_
#define _WRITE_FILE_H_

#include <stdio.h>

#ifdef WITH_CFITSIO
#include <fitsio.h>
#endif

/*============================================================================*\
                   Data structures for writing files by chunk
\*============================================================================*/

typedef struct {
  const char *fname;    /* name of the output file                    */
  FILE *fp;             /* pointer to the file stream                 */
  char *chunk;          /* buffer for writing file by chunks          */
  int size;             /* number of used characters of the buffer    */
  int max;              /* maximum number of characters of the buffer */
} OFILE;

#ifdef WITH_CFITSIO

typedef struct {
  fitsfile *fp;         /* pointer to the FITS file stream            */
  void **data;          /* buffer for writing the data table          */
  int *dtypes;          /* data types of table columns                */
  int *dsizes;          /* sizes of table columns in bytes            */
  int ncol;             /* number of columns                          */
  long nrow;            /* optimal number of rows written at once     */
  long ndata;           /* number of rows stored in the buffer        */
  long nsave;           /* number of rows that are already saved      */
} OFFILE;

#endif

/*============================================================================*\
                       Interfaces for ASCII file writing
\*============================================================================*/

/******************************************************************************
Function `output_init`:
  Initialise the interface for writing ASCII files.
Return:
  Address of the interface.
******************************************************************************/
OFILE *output_init(void);

/******************************************************************************
Function `output_newfile`:
  Flush the buffer to the existing file and open a new file.
Arguments:
  * `ofile`:    structure for writing ASCII files;
  * `fname`:    name of the file to be written to.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int output_newfile(OFILE *ofile, const char *fname);

/******************************************************************************
Function `output_destroy`:
  Deconstruct the interface for writing ASCII files.
Arguments:
  * `ofile`:    structure for writing ASCII files.
******************************************************************************/
void output_destroy(OFILE *ofile);

/******************************************************************************
Function `output_writeline`:
  Write a line to the buffer and save it to the file if necessary.
Arguments:
  * `ofile`:    structure for writing ASCII files;
  * `format`:   a string specifying how the data is interpreted;
  * `...`:      arguments specifying data to be saved.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int output_writeline(OFILE *ofile, const char *restrict format, ...);

/******************************************************************************
Function `output_flush`:
  Write the buffer string to file.
Arguments:
  * `ofile`:    structure for writing ASCII files.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int output_flush(OFILE *ofile);


#ifdef WITH_CFITSIO

/*============================================================================*\
                        Interfaces for FITS file writing
\*============================================================================*/

/******************************************************************************
Function `ofits_init`:
  Initialise the interface for FITS file writing.
Return:
  Address of the interface.
******************************************************************************/
OFFILE *ofits_init(void);

/******************************************************************************
Function `ofits_destroy`:
  Deconstruct the interface for writing FITS files.
Arguments:
  * `ofile`:    interface for FITS file writing.
******************************************************************************/
void ofits_destroy(OFFILE *ofile);

/******************************************************************************
Function `ofits_newfile`:
  Flush the buffer to the existing FITS file and open a new file.
Arguments:
  * `ofile`:    interface for FITS file writing;
  * `fname`:    name of the file to be written to;
  * `ncol`:     number of columns to be written;
  * `names`:    names of the columns;
  * `units`:    units of the columns;
  * `dtypes`:   data types of the columns.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int ofits_newfile(OFFILE *ofile, const char *fname, const int ncol,
    char **names, char **units, int *dtypes);

/******************************************************************************
Function `ofits_writeline`:
  Write a line to the buffer and save it to the FITS file if necessary.
Arguments:
  * `ofile`:    interface for FITS file writing;
  * `...`:      arguments specifying columns to be saved.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int ofits_writeline(OFFILE *ofile, ...);

/******************************************************************************
Function `ofits_flush`:
  Write the buffer string to file.
Arguments:
  * `ofile`:    interface for FITS file writing.
Return:
  Zero on success; non-zero on error.
******************************************************************************/
int ofits_flush(OFFILE *ofile);

#endif

#endif
