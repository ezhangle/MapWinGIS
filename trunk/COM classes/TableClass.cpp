/**************************************************************************************
 * File name: TableClass.cpp
 *
 * Project: MapWindow Open Source (MapWinGis ActiveX control) 
 * Description: implementation of CTableClass
 *
 **************************************************************************************
 * The contents of this file are subject to the Mozilla Public License Version 1.1
 * (the "License"); you may not use this file except in compliance with 
 * the License. You may obtain a copy of the License at http://www.mozilla.org/mpl/ 
 * See the License for the specific language governing rights and limitations
 * under the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ************************************************************************************** 
 * Contributor(s): 
 * (Open source contributors should list themselves and their modifications here). */

// 06/07/2007 - Tom Shanley (tws) - fixed a memory leak in EditCellValue
// 07/28/2009 - �ܴϻ�(Neio)   -- change the bahavior of SaveAs
//                                             -- Add Save Method for saving without exiting edit mode
//
// 09/02/2009 - Charles Yin - Change the behavior of StartEditing and StopEditing, make the saving faster 
//                            less memory usage.                              

#include "stdafx.h"
#include "TableClass.h"
#include <Oleauto.h> //used for Date Variant conversion

#include <algorithm>
#include <iterator>

#include "VarH.h"
//#include "UtilityFunctions.h"
#include "JenksBreaks.h"

#pragma warning(disable:4996)

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// *****************************************************
//	ParseExpressionCore()
// *****************************************************
void CTableClass::ParseExpressionCore(BSTR Expression, tkValueType returnType, BSTR* ErrorString, VARIANT_BOOL* retVal)
{
	*retVal = VARIANT_FALSE;
	//SysFreeString(*ErrorString);	// do we need it here?
	USES_CONVERSION;
	CString str = OLE2A(Expression);
	CExpression expr;	
	
	if (expr.ReadFieldNames(this))
	{
		CString err;
		if (expr.ParseExpression(str, true, err))
		{
			// testing with values of the first row; there can be inconsistent data types for example
			CComVariant var;
			int i = 0;

			for (int j = 0; j< expr.get_NumFields(); j++)
			{
				int fieldIndex = expr.get_FieldIndex(j);
				this->get_CellValue(fieldIndex, i, &var);
				switch (var.vt)
				{
					case VT_BSTR: expr.put_FieldValue(j, var.bstrVal); break;
					case VT_I4:	  expr.put_FieldValue(j, (double)var.lVal); break;
					case VT_R8:	  expr.put_FieldValue(j, (double)var.dblVal); break;
				}
			}
			
			// if expression returns true for the given record we'll save the index 
			CExpressionValue* result = expr.Calculate(err);	//new CExpressionValue();
			if ( result )
			{
				if (result->type != returnType )
				{
					if (returnType == vtString)
					{
						// there is no problem to convert any type to string						
						*retVal = VARIANT_TRUE;
					}
					else
					{
						*ErrorString = SysAllocString(L"Invalid resulting type");
					}
				}
				else if (result->type)
				{
					*retVal = VARIANT_TRUE;
				}
			}
			else
			{
				*ErrorString = A2BSTR(err);
			}
		}
		else
		{
			*ErrorString = A2BSTR(err);
		}
	}
	else
	{
		*ErrorString = SysAllocString(L"Failed to read field names");
	}
}

// *****************************************************
//		ParseExpression()
// *****************************************************
//  Checks the correctness of the expression syntax, but doesn't check the validity of data types
STDMETHODIMP CTableClass::ParseExpression(BSTR Expression, BSTR* ErrorString, VARIANT_BOOL* retVal)
{
	*retVal = VARIANT_FALSE;
	SysFreeString(*ErrorString);	// do we need it here?
	USES_CONVERSION;
	CString str = OLE2CA(Expression);
	CExpression expr;	
	
	if (expr.ReadFieldNames(this))
	{
		CString err;
		if (expr.ParseExpression(str, true, err))
		{
			*retVal = VARIANT_TRUE;
		}
		else
		{
			*ErrorString = A2BSTR(err);
		}
	}
	else
	{
		*ErrorString = SysAllocString(L"Failed to read field names");
	}
	return S_OK;
}

// *****************************************************************
//		TestExpression()
// *****************************************************************
// Checks syntax of expression and data types based on the first record in the table
STDMETHODIMP CTableClass::TestExpression(BSTR Expression, tkValueType ReturnType, BSTR* ErrorString, VARIANT_BOOL* retVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	ParseExpressionCore(Expression, ReturnType, ErrorString, retVal);
	return S_OK;
}


// *****************************************************************
//		Query()
// *****************************************************************
STDMETHODIMP CTableClass::Query(BSTR Expression, VARIANT* Result, BSTR* ErrorString, VARIANT_BOOL* retVal)
{
	*retVal = VARIANT_FALSE;
	SysFreeString(*ErrorString);	// do we need it here?
	USES_CONVERSION;
	CString str = OLE2CA(Expression);

	std::vector<long> indices;
	CString err;
	if (Query_(str, indices, err))
	{
		*ErrorString = SysAllocString(L"");
		if (indices.size() == 0)
		{
			*ErrorString = SysAllocString(L"Selection is empty");
			Result = NULL;
		}
		else
		{
			bool ret = Utility::Vector2SafeArray(&indices, VT_I4, Result);
			*retVal = ret?VARIANT_TRUE:VARIANT_FALSE;
		}
	}
	else
	{
		*ErrorString = A2BSTR(err);
	}
	
	
	return S_OK;
}

// *****************************************************************
//		Calculate()
// *****************************************************************
STDMETHODIMP CTableClass::Calculate(BSTR Expression, LONG RowIndex, VARIANT* Result, BSTR* ErrorString, VARIANT_BOOL* retVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState());
	
	*retVal = VARIANT_FALSE;
	Result->vt = VT_NULL;

	if( RowIndex < 0 || RowIndex >= RowCount())
	{
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		return S_OK;
	}

	USES_CONVERSION;
	CString str = OLE2A(Expression);

	CExpression expr;	
	if (expr.ReadFieldNames(this))
	{
		CString err;
		if (expr.ParseExpression(Expression, true, err))
		{
			CComVariant var;
			for (int j = 0; j< expr.get_NumFields(); j++)
			{
				int fieldIndex = expr.get_FieldIndex(j);
				this->get_CellValue(fieldIndex, RowIndex, &var);
				switch (var.vt)
				{
					case VT_BSTR: expr.put_FieldValue(j, var.bstrVal); break;
					case VT_I4:	  expr.put_FieldValue(j, (double)var.lVal); break;
					case VT_R8:	  expr.put_FieldValue(j, (double)var.dblVal); break;
				}
			}
				
			// no need to delete the result
			CExpressionValue* val = expr.Calculate(err);
			if (val)
			{
				if (val->type == vtBoolean)
				{
					Result->vt = VT_BOOL;
					Result->boolVal = val->bln;
				}
				else if (val->type == vtDouble)
				{
					Result->vt = VT_R8;
					Result->dblVal = val->dbl;
				}
				else if (val->type == vtString)
				{
					Result->vt = VT_BSTR;
					Result->bstrVal = A2BSTR(val->str);
				}
				*retVal = VARIANT_TRUE;
			}
		}
		else
		{
			*ErrorString = A2BSTR(err);
		}
	}
	else
	{
		*ErrorString = SysAllocString(L"Failed to read field names");
	}
	return S_OK;
}

// ********************************************************************
//			Query_()
// ********************************************************************
bool CTableClass::Query_(CString Expression, std::vector<long>& indices, CString& ErrorString)
{
	indices.clear();
	
	CExpression expr;	
	if (expr.ReadFieldNames(this))
	{
		CString err;
		if (expr.ParseExpression(Expression, true, err))
		{
			VARIANT var;
			VariantInit(&var);
			
			bool error = false;
			for (unsigned int i = 0; i < _rows.size(); i++)
			{
				for (int j = 0; j< expr.get_NumFields(); j++)
				{
					int fieldIndex = expr.get_FieldIndex(j);
					this->get_CellValue(fieldIndex, i, &var);
					switch (var.vt)
					{
						case VT_BSTR: expr.put_FieldValue(j, var.bstrVal); break;
						case VT_I4:	  expr.put_FieldValue(j, (double)var.lVal); break;
						case VT_R8:	  expr.put_FieldValue(j, (double)var.dblVal); break;
					}
				}
				
				// if expression returns true for the given record we'll save the index 
				CExpressionValue* result = expr.Calculate(err);	//new CExpressionValue();
				if ( result )
				{
					if (result->type == vtBoolean)
					{
						if (result->bln)
						{
							indices.push_back(i);
						}
					}
					else
					{
						ErrorString = "Resulting value isn't boolean";
						error = true;
					}
				}
				else
				{
					ErrorString = err;
					error = true;
					break;
				}
			}
			VariantClear(&var);

			return true;
		}
		else
		{
			ErrorString = err;
		}
	}
	else
	{
		ErrorString = "Failed to read field names";
	}
	return false;
}

// ********************************************************************
//			Calculate_()
// ********************************************************************
bool CTableClass::Calculate_(CString Expression, std::vector<CString>& results, CString& ErrorString, int rowIndex)
{
	results.clear();
	
	CExpression expr;	
	if (expr.ReadFieldNames(this))
	{
		CString err;
		if (expr.ParseExpression(Expression, true, err))
		{
			VARIANT var;
			VariantInit(&var);
			
			bool error = false;
			CString str;

			int start = (rowIndex == -1) ? 0 : rowIndex;
			int end = (rowIndex == -1) ? int(_rows.size()) : rowIndex + 1;

			for (int i = start; i < end; i++)
			{
				for (int j = 0; j< expr.get_NumFields(); j++)
				{
					int fieldIndex = expr.get_FieldIndex(j);
					this->get_CellValue(fieldIndex, i, &var);
					switch (var.vt)
					{
						case VT_BSTR:
							expr.put_FieldValue(j, var.bstrVal); 
							break;
						case VT_I4:	  
							str.Format("%d", var.lVal);
							expr.put_FieldValue(j, str); 
							break;
						case VT_R8:	  
							str.Format("%g", var.dblVal);
							expr.put_FieldValue(j, str); 
							break;
					}
				}
				
				// if expression returns true for the given record we'll save the index 
				CExpressionValue* result = expr.Calculate(err);
				if ( result )
				{
					if (result->type == vtString)
					{
						str = result->str;
						results.push_back(str);
					}
					else if (result->type == vtBoolean)
					{
						str = result->bln ? "true" : "false";
						results.push_back(str);
					}
					else
					{
						str.Format("%f", result->dbl);
						results.push_back(str);
						
						//ErrorString = "Invalid result type";
						//error = true;
					}
				}
				else
				{
					ErrorString = err;
					error = true;
					break;
				}
			}
			VariantClear(&var);

			return true;
		}
		else
		{
			ErrorString = err;
		}
	}
	else
	{
		ErrorString = "Failed to read field names";
	}
	return false;
}


// ****************************************************************
//	  AnalyzeExpressions()
// ****************************************************************
// Analyses list of expressios and returns vector of size equal to numShapes where 
// for each shape index a category is specified. If the given shape didn't fall into
// any category -1 is used. The first categories in the list have higher priority
// Results vector with certain categories can be provided by caller; those categories won't be changed
void CTableClass::AnalyzeExpressions(std::vector<CString>& expressions, std::vector<int>& results)
{
	//std::vector<int>* results = new std::vector<int>;
	//results->resize(_rows.size(), -1);
	
	CExpression expr;	
	if (expr.ReadFieldNames(this))
	{
		for (unsigned int categoryId = 0; categoryId < expressions.size(); categoryId++)
		{
			if (expressions[categoryId] != "")
			{
				CString err;
				if (expr.ParseExpression(expressions[categoryId], true, err))
				{
					VARIANT var;
					VariantInit(&var);
					
					for (unsigned int i = 0; i < _rows.size(); i++)
					{
						if (results[i] == -1)
						{
							for (long j = 0; j< expr.get_NumFields(); j++)
							{
								int fieldIndex = expr.get_FieldIndex(j);
								this->get_CellValue(fieldIndex, i, &var);
								switch (var.vt)
								{
									case VT_BSTR: expr.put_FieldValue(j, var.bstrVal); break;
									case VT_I4:	  expr.put_FieldValue(j, (double)var.lVal); break;
									case VT_R8:	  expr.put_FieldValue(j, (double)var.dblVal); break;
								}
							}
						
							// if expression returns true for the given record we'll save the index 
							CExpressionValue* result = expr.Calculate(err);
							if ( result )
							{
								if (result->type == vtBoolean && result->bln)
								{
									results[i] = categoryId;
								}
							}
						}
					}
					VariantClear(&var);
				}
			}
		}
	}
}

// ***********************************************************
//		get_Num_rows
// ***********************************************************
STDMETHODIMP CTableClass::get_NumRows(long *pVal)
{
	*pVal = RowCount();
	return S_OK;
}
long CTableClass::RowCount()
{
    return _rows.size();
}

// ***********************************************************
//		get_NumFields
// ***********************************************************
STDMETHODIMP CTableClass::get_NumFields(long *pVal)
{
	*pVal = FieldCount();
	return S_OK;
}
long CTableClass::FieldCount()
{
    return _fields.size();
}

// ***********************************************************
//		get_Field
// ***********************************************************
STDMETHODIMP CTableClass::get_Field(long FieldIndex, IField **pVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;

	if( FieldIndex < 0 || FieldIndex >= (int)FieldCount() )
	{	
		*pVal = NULL;
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		
	}
	else
	{
		((CField*)_fields[FieldIndex].field)->_table = this;
		*pVal = _fields[FieldIndex].field;
		_fields[FieldIndex].field->AddRef();
	}
	return S_OK;
}

// ***********************************************************
//		get_CellValue
// ***********************************************************
STDMETHODIMP CTableClass::get_CellValue(long FieldIndex, long RowIndex, VARIANT *pVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
    USES_CONVERSION;

	if( FieldIndex < 0 || FieldIndex >= FieldCount() || RowIndex < 0 || RowIndex >= RowCount())
	{	
		VARIANT var;
		VariantInit(&var);
		var.vt = VT_EMPTY;
		pVal = &var;
		//pVal = NULL;
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		return S_OK;
	}

	if( ReadRecord(RowIndex) && _rows[RowIndex].row != NULL)
	{
        VARIANT* var = _rows[RowIndex].row->values[FieldIndex];
		if (var != NULL)
		{
			if (var->vt != VT_NULL)
			{
				VariantCopy(pVal, _rows[RowIndex].row->values[FieldIndex]);
			}
			else
			{
				VARIANT var;
				VariantInit(&var);
				var.vt = VT_EMPTY;
				pVal = &var;
			}
		}
		else
		{
			/*VARIANT var;
			VariantInit(&var);
			var.vt = VT_EMPTY;
			pVal = &var;*/
			pVal = NULL;
		}
	}
	else
	{
		/*VARIANT var;
		VariantInit(&var);
		var.vt = VT_EMPTY;
		pVal = &var;*/
		pVal = NULL;
	}
	return S_OK;
}

// ***********************************************************
//		get_CellValue
// ***********************************************************
STDMETHODIMP CTableClass::get_EditingTable(VARIANT_BOOL *pVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	*pVal = isEditingTable?VARIANT_TRUE:VARIANT_FALSE;
	return S_OK;
}

// ***********************************************************
//		get_LastErrorCode
// ***********************************************************
STDMETHODIMP CTableClass::get_LastErrorCode(long *pVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	*pVal = lastErrorCode;
	lastErrorCode = tkNO_ERROR;
	return S_OK;
}

// ***********************************************************
//		get_ErrorMsg
// ***********************************************************
STDMETHODIMP CTableClass::get_ErrorMsg(long ErrorCode, BSTR *pVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;
	*pVal = A2BSTR(ErrorMsg(ErrorCode));
	return S_OK;
}

// ***********************************************************
//		get_CdlgFilter
// ***********************************************************
STDMETHODIMP CTableClass::get_CdlgFilter(BSTR *pVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;
	*pVal = A2BSTR("dBase Files (*.dbf)|*.dbf");
	return S_OK;
}

// ***********************************************************
//		get/put_GlobalCallback
// ***********************************************************
STDMETHODIMP CTableClass::get_GlobalCallback(ICallback **pVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	*pVal = globalCallback;
	if( globalCallback )
		globalCallback->AddRef();
	return S_OK;
}
STDMETHODIMP CTableClass::put_GlobalCallback(ICallback *newVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	put_ComReference(newVal, (IDispatch**)&newVal);
	return S_OK;
}

// ***********************************************************
//		get/put_Key
// ***********************************************************
STDMETHODIMP CTableClass::get_Key(BSTR *pVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;
	*pVal = OLE2BSTR(key);
	return S_OK;
}

STDMETHODIMP CTableClass::put_Key(BSTR newVal)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;
	::SysFreeString(key);
	key = OLE2BSTR(newVal);
	return S_OK;
}

// ***********************************************************
//		Open
// ***********************************************************
STDMETHODIMP CTableClass::Open(BSTR dbfFilename, ICallback *cBack, VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;
	
	*retval = VARIANT_FALSE;
	if ((cBack != NULL) && (globalCallback == NULL))
	{
		globalCallback = cBack;
		cBack->AddRef();
	}

	Close(retval);

	if( *retval)
	{	
		if( Utility::fileExistsUnicode( OLE2A(dbfFilename) ) == FALSE )
		{	
			ErrorMessage(tkDBF_FILE_DOES_NOT_EXIST);
			return S_OK;
		}
		
		// checking writing permissions
		LPCSTR name = OLE2CA(dbfFilename);
		DWORD dwAttrs = GetFileAttributes(name);
		if (dwAttrs == INVALID_FILE_ATTRIBUTES)
		{
			ErrorMessage(tkCANT_OPEN_DBF);
			return S_OK;
		}
		bool readOnly = (dwAttrs & FILE_ATTRIBUTE_READONLY);

		// opening
		if (!readOnly)
			dbfHandle = DBFOpen_MW(OLE2CA(dbfFilename),"rb+");
		else
			dbfHandle = DBFOpen_MW(OLE2CA(dbfFilename),"rb");
		
		if( dbfHandle == NULL )
		{	
			ErrorMessage(tkCANT_OPEN_DBF);
			return S_OK;
		}

		filename = OLE2CA(dbfFilename);
		*retval = VARIANT_TRUE;
	
		//After open the dbf file, load all _fields info and create spatial row indices 
		//with FieldWrapper and RecordWrapper help classes.
		LoadDefault_fields();
		LoadDefault_rows();
	}
	return S_OK;
}

// **************************************************************
//	  CreateNew()
// **************************************************************
STDMETHODIMP CTableClass::CreateNew(BSTR dbfFilename, VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;
	
	// closing the existing table
	Close(retval);
	
	if( *retval == VARIANT_FALSE )
	{	
		return S_OK;
	}
	else
	{	
		if( Utility::fileExistsUnicode( OLE2A(dbfFilename) ) )
		{	
			*retval = VARIANT_FALSE;
			ErrorMessage(tkDBF_FILE_EXISTS);
			return S_OK;
		}
				
		filename = OLE2CA(dbfFilename);
		isEditingTable = TRUE;
		*retval = VARIANT_TRUE;
	}
	return S_OK;
}

// **************************************************************
//	  SaveToFile()
// **************************************************************
bool CTableClass::SaveToFile(const CString& dbfFilename, bool updateFileInPlace, ICallback* cBack)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;
	
	if (globalCallback == NULL && cBack != NULL)
	{
		globalCallback = cBack;
		cBack->AddRef();
	}

	if( dbfHandle == NULL && isEditingTable == FALSE )
	{	
		ErrorMessage(lastErrorCode);
		return false;		
	}

	DBFInfo * newdbfHandle;
	if(updateFileInPlace)
	{
		newdbfHandle = dbfHandle;
		
		// DOESN'T WORK: dbfHandle can't be modified directly

		// in case some of the field attributes changed
		//FieldType type;
		//long width, precision;
		//
		//for (int i = 0; i < FieldCount(); i++)
		//{
		//	IField* field = _fields[i].field;
		//	if (((CField*)field)->isUpdated)
		//	{
		//		field->get_Type(&type);
		//		field->get_Width(&width);
		//		field->get_Precision(&precision);
		//		
		//		dbfHandle->panFieldSize[dbfHandle->nFields-1] = width;
		//		dbfHandle->panFieldDecimals[dbfHandle->nFields-1] = precision;
		//		dbfHandle->pachFieldType[dbfHandle->nFields-1] = type;
		//		
		//		// saving name
		//		BSTR fname;
		//		field->get_Name(&fname);
		//		const char* name = OLE2CA(fname);

		//		char* pszFInfo = dbfHandle->pszHeader + 32 * (dbfHandle->nFields-1);

		//		if( (int) strlen(name) < 10 )
		//			strncpy( pszFInfo, name, strlen(name));
		//		else
		//			strncpy( pszFInfo, name, 10);
		//	}
		//}
	}
	else
	{
		if ( Utility::fileExistsUnicode(dbfFilename) != FALSE )
		{	
			ErrorMessage(tkDBF_FILE_EXISTS);
			return false;
		}

		newdbfHandle = DBFCreate(dbfFilename);
		if( newdbfHandle == NULL )
		{	
			ErrorMessage(tkCANT_CREATE_DBF);
			return false;
		}
	
		for( int i = 0; i < FieldCount(); i++ )
		{	
			IField * field = NULL;
			get_Field(i,&field);
			BSTR fname;
			FieldType type;
			long width, precision;
			field->get_Name(&fname);
			field->get_Type(&type);
			field->get_Width(&width);
			field->get_Precision(&precision);

			if( type == DOUBLE_FIELD )
			{
				if( precision <= 0 ) 
					precision = 1;
			}
			else
				precision = 0;

			DBFAddField(newdbfHandle,OLE2CA(fname),(DBFFieldType)type,width,precision);
			field->Release(); 
			::SysFreeString(fname);
		}
	}
	
	long percent = 0, newpercent = 0;
	long currentRowIndex = -1;

	for( long rowIndex = 0; rowIndex < RowCount(); rowIndex++ )
	{
		newpercent = static_cast<long>((rowIndex+1.0)/RowCount()*100); 
		if( newpercent > percent)
		{	
			percent = newpercent;
			if( cBack != NULL ) 
				cBack->Progress(OLE2BSTR(key),percent,A2BSTR("Writing .dbf"));	
		}

		//if updating existing file, only write out modified records
		if  (updateFileInPlace && 
			(_rows[rowIndex].row == NULL || _rows[rowIndex].row->status() != TableRow::DATA_MODIFIED))
		{
			currentRowIndex++;
			continue;
		}

        if (!WriteRecord(newdbfHandle, rowIndex, ++currentRowIndex))
        {
		    ErrorMessage(tkDBF_CANT_ADD_DBF_FIELD);
		    return false;
        }

		if (updateFileInPlace)
			_rows[rowIndex].row->SetDirty(TableRow::DATA_CLEAN);
		else
			ClearRow(rowIndex);
	}

	//Flush all of the records
	if (!updateFileInPlace)
		DBFClose(newdbfHandle);

	return true;
}

// **************************************************************
//	  SaveToFile()
// **************************************************************
STDMETHODIMP CTableClass::SaveAs(BSTR dbfFilename, ICallback *cBack, VARIANT_BOOL *retval)
{
	USES_CONVERSION;
	if (!SaveToFile( OLE2A(dbfFilename), false, cBack))
	{
		*retval = VARIANT_FALSE;
		return S_OK;
	}

	if( dbfHandle != NULL )
		DBFClose(dbfHandle);
	dbfHandle = NULL;

	Open(dbfFilename,cBack,retval);		
	*retval = VARIANT_TRUE;

	return S_OK;
}

// **************************************************************
//	  Close()
// **************************************************************
STDMETHODIMP CTableClass::Close(VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	*retval = VARIANT_TRUE;

	for( int i = 0; i < FieldCount(); i++ )
	{	
        if (_fields[i].field != NULL)
        {
			((CField*)_fields[i].field)->_table = NULL; // lsu: if the field is used somewhere else, we must not refer to this table
			_fields[i].field->Release();
		    _fields[i].field = NULL;
        }
	}
	_fields.clear();

	for( int j = 0; j < RowCount(); j++ )
	{	
        if( _rows[j].row != NULL )
            delete _rows[j].row;
	}
	_rows.clear();
	
	filename = "";
	if( dbfHandle != NULL )
	{
		DBFClose(dbfHandle);
		dbfHandle = NULL;
	}
	
	*retval = VARIANT_TRUE;
	/*if( !isEditingTable )
		ErrorMessage(tkDBF_IN_EDIT_MODE);

	*retval = !isEditingTable ? VARIANT_FALSE : VARIANT_TRUE;*/
	
	return S_OK;
}

// **************************************************************
//	  Close()
// **************************************************************
void CTableClass::LoadDefault_fields()
{
    USES_CONVERSION;
    if (!_fields.empty())
    {
        for (std::vector<FieldWrapper>::iterator i = _fields.begin(); i!= _fields.end(); i++)
        {
			if ((*i).field != NULL)
			{
				((CField*)(*i).field)->_table = NULL;
				(*i).field->Release();
			}
        }
        _fields.clear();
    }
    
	if (dbfHandle == NULL) return;
    long num_fields = DBFGetFieldCount(dbfHandle);
	char * fname = new char[MAX_BUFFER];
	int fwidth, fdecimals;
	DBFFieldType type;
	IField * field = NULL;

    for( long i = 0; i < num_fields; i++ )
	{
		type = DBFGetFieldInfo(dbfHandle,i,fname,&fwidth,&fdecimals);

		CoCreateInstance(CLSID_Field,NULL,CLSCTX_INPROC_SERVER,IID_IField,(void**)&field);
		field->put_GlobalCallback(globalCallback);
		field->put_Name(A2BSTR(fname));
		field->put_Width(fwidth);
		field->put_Precision(fdecimals);
		field->put_Type((FieldType)type);
		
		FieldWrapper fw;
        fw.oldIndex = i;
        fw.field = field;
		_fields.push_back(fw);
		((CField*)field)->_table = this;
	}

	if( fname != NULL) delete [] fname;
	fname = NULL;
}

// **************************************************************
//	  Close()
// **************************************************************
//Initialize RecordWrapper array and set the TableRow pointer to NULL
void CTableClass::LoadDefault_rows()
{	
    if (!_rows.empty())
    {
        for (std::vector<RecordWrapper>::iterator i = _rows.begin(); i!= _rows.end(); i++)
        {
            if ( (*i).row != NULL) delete (*i).row;
        }
        _rows.clear();
    }
    
	if (dbfHandle == NULL) return;
	long num_rows = DBFGetRecordCount(dbfHandle);
    for (long i = 0; i < num_rows; i++)
    {
        RecordWrapper rw;
        rw.oldIndex = i;
        rw.row = NULL;
        _rows.push_back(rw);
    }
}

// **************************************************************
//	  EditClear()
// **************************************************************
STDMETHODIMP CTableClass::EditClear(VARIANT_BOOL *retval)
{	
    //Reset all editing bits and reload original _fields info and reinitialize the RowWrapper array
	AFX_MANAGE_STATE(AfxGetStaticModuleState())

    LoadDefault_fields();
    LoadDefault_rows();

    needToSaveAsNewFile = false;
	*retval = VARIANT_TRUE;
	return S_OK;
}

// **************************************************************
//	  EditInsertField()
// **************************************************************
STDMETHODIMP CTableClass::EditInsertField(IField *Field, long *FieldIndex, ICallback *cBack, VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	*retval = VARIANT_FALSE;
	
	if (cBack != NULL && globalCallback == NULL)
	{
		globalCallback = cBack;
		cBack->AddRef();
	}

	if( isEditingTable == FALSE )
	{	
		ErrorMessage(tkDBF_NOT_IN_EDIT_MODE);
		return S_OK;
	}
	
	// we'll help the user and correct invalid index
	if( *FieldIndex < 0 )
	{
		*FieldIndex = 0;
	}
	else if( *FieldIndex > FieldCount() )
	{
		*FieldIndex = _fields.size();
	}

	FieldType type;
	Field->get_Type(&type);

	for( long i = 0; i < RowCount(); i++ )
    {
        // lsu 29-oct-2009: fixing the bug 1459 (crash after adding field)
		// force reading all the records in memory to get rid of the unitialized values
		if (_rows[i].row == NULL) 
			ReadRecord(i);
	
		VARIANT * val = NULL;
        val = new VARIANT;
	    VariantInit(val);

	    if (type == STRING_FIELD)	 
		{
			val->vt = VT_BSTR;
			val->bstrVal = A2BSTR("");
		}
		else	
			val->vt = VT_NULL;

		_rows[i].row->values.insert( _rows[i].row->values.begin() + *FieldIndex, val);
    }

    FieldWrapper fw;
    fw.oldIndex = -1;
    fw.field = Field;
    
	_fields.insert( _fields.begin() + *FieldIndex, fw );
    _fields[*FieldIndex].field->AddRef();	
	*retval = VARIANT_TRUE;	  
	
	// in the field class we should know about table editing state
	((CField*)_fields[*FieldIndex].field)->_table = this;

    needToSaveAsNewFile = true;
	return S_OK;
}

// **************************************************************
//	  EditInsertField()
// **************************************************************
STDMETHODIMP CTableClass::EditReplaceField(long FieldIndex, IField *newField, ICallback *cBack, VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	
	if( FieldIndex < 0 || FieldIndex >= (int)_fields.size() )
	{	
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		return S_OK;
	}
	
	if (_fields[FieldIndex].field == newField)
	{
		// it's the same field; no need to do anything
		return S_OK;
	}

	// deleting field
	EditDeleteField(FieldIndex, cBack, retval);
	
	// inserting the new one
	if (retval)
	{
		EditInsertField(newField, &FieldIndex, cBack, retval);
	}
	
	return S_OK;
}

// *******************************************************************
//		EditDeleteField()
// *******************************************************************
STDMETHODIMP CTableClass::EditDeleteField(long FieldIndex, ICallback *cBack, VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	*retval = VARIANT_FALSE;
	
	if(globalCallback == NULL && cBack != NULL)
	{
		globalCallback = cBack;
		globalCallback->AddRef();
	}

	if( isEditingTable == FALSE )
	{	
		ErrorMessage(tkDBF_NOT_IN_EDIT_MODE);
		return S_OK;
	}

	if( FieldIndex < 0 || FieldIndex >= (int)_fields.size() )
	{	
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		return S_OK;
	}

	for( long i = 0; i < RowCount(); i++ )
    {	
        if( _rows[i].row != NULL && _rows[i].row->values[FieldIndex] != NULL )
        {
			VariantClear(_rows[i].row->values[FieldIndex]); //added by Rob Cairns 4-Jan-06
            _rows[i].row->values[FieldIndex] = NULL;
            _rows[i].row->values.erase( _rows[i].row->values.begin() + FieldIndex );		
        }
	}

	_fields[FieldIndex].field->Release();
    _fields[FieldIndex].field = NULL;
	_fields.erase( _fields.begin() + FieldIndex );

	//DeleteField operation can't be saved into the original dbf file.
    needToSaveAsNewFile = true;

	*retval = VARIANT_TRUE;		
	return S_OK;
}

// *******************************************************************
//		EditInsertRow()
// *******************************************************************
STDMETHODIMP CTableClass::EditInsertRow(long * RowIndex, VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	*retval = VARIANT_FALSE;
	
	if( isEditingTable == FALSE )
	{	
		ErrorMessage(tkDBF_NOT_IN_EDIT_MODE);
		return S_OK;
	}
	
	// will make users life easy and correct wrong value
	if( *RowIndex < 0 )
	{
		*RowIndex = 0;
	}
	else if( *RowIndex > RowCount() )
	{
		*RowIndex = RowCount();
	}
	
	TableRow * tr = new TableRow();
    tr->SetDirty(TableRow::DATA_INSERTED);

	for( long i = 0; i < FieldCount(); i++ )
	{
		VARIANT * val = NULL;
        val = new VARIANT();
	    VariantInit(val);

		FieldType type = GetFieldType(i);
	    if (type == STRING_FIELD)	 
		{
			val->vt = VT_BSTR;
			val->bstrVal = A2BSTR("");
		}
		else	
			val->vt = VT_NULL;

		tr->values.push_back(val);
	}
	RecordWrapper rw;
	rw.row = tr;
	rw.oldIndex = -1;
	_rows.insert( _rows.begin() + *RowIndex, rw);

    needToSaveAsNewFile = true;
	*retval = VARIANT_TRUE;		
    return S_OK;
}


// *******************************************************************
//		ReadRecord()
// *******************************************************************
//Read one row values and cached into the RecordWrapper array.
bool CTableClass::ReadRecord(long RowIndex)
{			   
	USES_CONVERSION;

    if (RowIndex < 0 && RowIndex >= RowCount())
        return false;
	
	if (_rows[RowIndex].row != NULL)		// lsu: I think it's enough to test only this
		return true;

    if( dbfHandle == NULL )
	{	
		ErrorMessage(tkFILE_NOT_OPEN);
		return false;
	}

	long percent = 0, newpercent = 0;
    _rows[RowIndex].row = new TableRow();
		
	for(int i = 0; i < FieldCount(); i++ )
	{	
        FieldType type = GetFieldType(i);
		VARIANT * val = NULL;
        
        if (_fields[i].oldIndex != -1)
        {
            val = new VARIANT;
		    VariantInit(val);
		    bool isNull = false;
		
    		//Rob Cairns 14/2/2006
            if (DBFIsAttributeNULL(dbfHandle,_rows[RowIndex].oldIndex,_fields[i].oldIndex) == 1)
		    {	
				isNull = true;
		    }			
		
		    if( type == STRING_FIELD )
		    {	
			    if (isNull)
			    {	
				    val->vt = VT_BSTR;
				    val->bstrVal = A2BSTR("");
			    }
			    else
			    {	
					val->vt = VT_BSTR;
				    val->bstrVal = A2BSTR( DBFReadStringAttribute(dbfHandle,_rows[RowIndex].oldIndex,_fields[i].oldIndex) );
			    }
		    }
		    else if( type == INTEGER_FIELD )
		    {	
			    if (isNull)   
				{
					val->vt = VT_NULL;
				}
			    else
			    {	
					int res = DBFReadIntegerAttribute(dbfHandle,_rows[RowIndex].oldIndex,_fields[i].oldIndex);
				    val->vt = VT_I4;
				    val->lVal = res;
			    }
		    }
		    else if( type == DOUBLE_FIELD )
		    {	
			    if (isNull)
				{
					val->vt = VT_NULL;
				}
			    else
			    {	
					double res = DBFReadDoubleAttribute(dbfHandle,_rows[RowIndex].oldIndex,_fields[i].oldIndex);
				    val->vt = VT_R8;
				    val->dblVal = res;
			    }
		    }
            _rows[RowIndex].row->SetDirty(TableRow::DATA_CLEAN);
        }
		_rows[RowIndex].row->values.push_back(val);
	}

	return (_rows[RowIndex].row != NULL);
}

// *******************************************************************
//		WriteRecord()
// *******************************************************************
//Write a cached RecordWrapper into dbf file 
bool CTableClass::WriteRecord(DBFInfo* dbfHandle, long fromRowIndex, long toRowIndex)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;

    if( dbfHandle == NULL )
	{	
		ErrorMessage(tkFILE_NOT_OPEN);
		return false;
	}

    if (fromRowIndex < 0 || fromRowIndex >= RowCount())
        return false;
    
    char * nonstackString = NULL;

	for(long i = 0; i < FieldCount(); i++ )
	{	
		FieldType type = GetFieldType(i);				
    	long precision = GetFieldPrecision(i);

		VARIANT val;
		VariantInit(&val);
		get_CellValue(i,fromRowIndex,&val);
		if( type == FTString )
		{	
			if( val.vt == VT_BSTR )
			{	
				nonstackString = Utility::SYS2A(val.bstrVal);
				DBFWriteStringAttribute(dbfHandle,toRowIndex,i,nonstackString);
				delete [] nonstackString;
				nonstackString = NULL;	
			}
			else if( val.vt == VT_I4 )
			{	
				CString cval;
				cval.Format("%d",val.dblVal);
				DBFWriteStringAttribute(dbfHandle,toRowIndex,i,cval);
			}
			else if( val.vt == VT_R8 )
			{	
				CString cval;
				cval.Format("%i",val.lVal);
				DBFWriteStringAttribute(dbfHandle,toRowIndex,i,cval);
			}
			else
			{	
				DBFWriteStringAttribute(dbfHandle,toRowIndex,i,"");
			}
		}
		else if( type == FTInteger )
		{	
			if( val.vt == VT_BSTR )
			{	
				nonstackString = Utility::SYS2A(val.bstrVal);							
				long lval = atoi(nonstackString);
				DBFWriteIntegerAttribute(dbfHandle,toRowIndex,i,lval);
				delete [] nonstackString;
				nonstackString = NULL;
			}
			else if( val.vt == VT_I4 )
			{	
				DBFWriteIntegerAttribute(dbfHandle,toRowIndex,i,val.lVal);
			}
			else if( val.vt == VT_R8 )
			{	
				DBFWriteIntegerAttribute(dbfHandle,toRowIndex,i,(int)val.dblVal);
			}
			else if( val.vt == VT_NULL )
			{	
				DBFWriteNULLAttribute(dbfHandle,toRowIndex,i); 
			}
			else
			{	
				DBFWriteIntegerAttribute(dbfHandle,toRowIndex,i,0);
			}
		}
		else if( type == FTDouble )
		{	
			if( val.vt == VT_BSTR )
			{	
				nonstackString = Utility::SYS2A(val.bstrVal);
				double dblval = Utility::atof_custom(nonstackString);
				DBFWriteDoubleAttribute(dbfHandle,toRowIndex,i,dblval);
				delete [] nonstackString;
				nonstackString = NULL;
			}
			else if( val.vt == VT_I4 )
			{	
				DBFWriteDoubleAttribute(dbfHandle,toRowIndex,i,val.lVal);
			}
			else if( val.vt == VT_R8 )
			{	
				DBFWriteDoubleAttribute(dbfHandle,toRowIndex,i,val.dblVal);
			}
			else if( val.vt == VT_NULL )
			{	
				DBFWriteNULLAttribute(dbfHandle,toRowIndex,i);
			}
			else
			{	
				DBFWriteDoubleAttribute(dbfHandle,toRowIndex,i,0.0);
			}
		}
		VariantClear(&val);
	}
    return true;
}

// *******************************************************************
//		EditCellValue()
// *******************************************************************
STDMETHODIMP CTableClass::EditCellValue(long FieldIndex, long RowIndex, VARIANT newVal, VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;
	*retval = VARIANT_FALSE;

	if( isEditingTable == FALSE )
	{	
		ErrorMessage(tkDBF_NOT_IN_EDIT_MODE);
		return S_OK;
	}

	if( FieldIndex < 0 || FieldIndex >= FieldCount() || RowIndex < 0 || RowIndex >= RowCount())
	{	
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		return S_OK;
	}

	if( newVal.vt == VT_I2 )
	{	
		long val = newVal.iVal;
		newVal.vt = VT_I4;
		newVal.lVal = val;
	}
	else if (newVal.vt == VT_I8)
	{
		long val = newVal.lVal;
		newVal.vt = VT_I4;
		newVal.lVal = val;
	}
	else if( newVal.vt == (VT_BYREF|VT_I8) )
	{	
		LONGLONG val = *(newVal.pllVal);
		newVal.vt = VT_I4;
		newVal.lVal = (long)val;
	}
	else if( newVal.vt == (VT_BYREF|VT_I2) )
	{	
		long val = *(newVal.piVal);
		newVal.vt = VT_I4;
		newVal.lVal = val;
	}
	else if( newVal.vt == (VT_BYREF|VT_I4) )
	{	
		long val = *(newVal.plVal);
		newVal.vt = VT_I4;
		newVal.lVal = val;
	}
	else if( newVal.vt == VT_R4 )
	{	
		double val = newVal.fltVal;
		newVal.vt = VT_R8;
		newVal.dblVal = val;
	}
	else if( newVal.vt == (VT_BYREF|VT_R4) )
	{	
		double val = *(newVal.pfltVal);
		newVal.vt = VT_R8;
		newVal.dblVal = val;
	}
	else if( newVal.vt == (VT_BYREF|VT_R8) )
	{	
		double val = *(newVal.pdblVal);
		newVal.vt = VT_R8;
		newVal.dblVal = val;
	}
	else if( newVal.vt == (VT_BYREF|VT_BSTR) )
	{	
		BSTR val = OLE2BSTR(*(newVal.pbstrVal));
		newVal.vt = VT_BSTR;
		newVal.bstrVal = val;
	}
	else if( newVal.vt == VT_DATE )
	{
		BSTR val;
		LCID localeID = MAKELCID(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US), SORT_DEFAULT);
		VarBstrFromDate(newVal.date,localeID,LOCALE_NOUSEROVERRIDE,&val);
		newVal.vt = VT_BSTR;
		newVal.bstrVal = val;
		SysFreeString(val);
	}

	/*
	VT_UI1;
    VT_I2;  
    VT_I4;   
    VT_R4;   
    VT_R8;   
    VT_BOOL;
    VT_ERROR;
    VT_CY;
    VT_DATE;
    VT_BSTR;
    VT_UNKNOWN;
    VT_DISPATCH;
    VT_ARRAY;
    VT_BYREF|VT_UI1;
    VT_BYREF|VT_I2;
    VT_BYREF|VT_I4;
    VT_BYREF|VT_R4;
    VT_BYREF|VT_R8;
    VT_BYREF|VT_BOOL;
    VT_BYREF|VT_ERROR;
    VT_BYREF|VT_CY;
    VT_BYREF|VT_DATE;
    VT_BYREF|VT_BSTR;
    VT_BYREF|VT_UNKNOWN;
    VT_BYREF|VT_DISPATCH;
    VT_ARRAY;
    VT_BYREF|VT_VARIANT;
    */

	// Darrel Brown, 10/16/2003 Added support for null cell values
	if( newVal.vt != VT_I4 && newVal.vt != VT_R8 && newVal.vt != VT_BSTR && newVal.vt != VT_NULL )
	{	
		ErrorMessage(tkINCORRECT_VARIANT_TYPE);
	}
	else
    {	
        if (_rows[RowIndex].row == NULL)
            ReadRecord(RowIndex);
        
        if (_rows[RowIndex].row != NULL)
        {
            if( _rows[RowIndex].row->values[FieldIndex] == NULL )
		    {   
				// tws  6/7/7 : VariantInit DOES NOT free any old value, but VariantClear and VariantCopy DO 
			    // so only use VariantInit when it is NEW, otherwise it will leak BSTRs
			    // this pair of braces saved 80MB of mem leak on table edit 50k shapes with 40 _fields
			    _rows[RowIndex].row->values[FieldIndex] = new VARIANT;
			    VariantInit(_rows[RowIndex].row->values[FieldIndex]);
		    }
		    VariantCopy(_rows[RowIndex].row->values[FieldIndex], &newVal);

		    //Change the width of the field
		    IField * field = NULL;
		    get_Field(FieldIndex,&field);
		    FieldType type;
		    long precision, width;
		    field->get_Type(&type);
		    field->get_Width(&width);
		    field->get_Precision(&precision);

		    long valWidth = 0;
		    if( newVal.vt == VT_BSTR )
		    {	
				CString cval(OLE2CA(newVal.bstrVal));
			    valWidth = cval.GetLength();
		    }
		    else if( newVal.vt == VT_I4 )
		    {	
				CString cval;
			    cval.Format("%i",newVal.lVal);
			    valWidth = cval.GetLength();
		    }
		    else if( newVal.vt == VT_R8 )
		    {	
				CString cval;
			    CString fmat;
			    fmat.Format("%i",precision);
			    cval.Format("%." + fmat + "d",precision,newVal.dblVal);	
			    valWidth = cval.GetLength();
		    }
		    if( valWidth > width )
			    field->put_Width(valWidth);

		    field->Release();  
            _rows[RowIndex].row->SetDirty(TableRow::DATA_MODIFIED);
        }
	}

	*retval = VARIANT_TRUE;
	return S_OK;
}

// *********************************************************
//		StartEditingTable ()
// *********************************************************
STDMETHODIMP CTableClass::StartEditingTable(ICallback *cBack, VARIANT_BOOL *retval)
{  
    // StartEditingTable now just simplly set the editing flag, not read all records into memory
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;

	if( dbfHandle == NULL )
	{	
		ErrorMessage(tkFILE_NOT_OPEN);
		*retval = VARIANT_FALSE;
		return S_OK;
	}

	if( isEditingTable != TRUE )
	{	
		isEditingTable = TRUE;
		needToSaveAsNewFile = false;
	}

	*retval = VARIANT_TRUE;
	return S_OK;
}

// *****************************************************************
//		StopEditingTable()
// *****************************************************************
STDMETHODIMP CTableClass::StopEditingTable(VARIANT_BOOL ApplyChanges, ICallback *cBack, VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	USES_CONVERSION;
	*retval = VARIANT_FALSE;

	if (globalCallback == NULL && cBack != NULL)
	{
		globalCallback = cBack;
		globalCallback->AddRef();
	}

	if( dbfHandle == NULL )
	{	
		if( isEditingTable != FALSE )
		{	
			if( ApplyChanges != VARIANT_FALSE )
				SaveAs(filename.AllocSysString(),cBack,retval);
			else
				EditClear(retval);
			return S_OK;
		}
		else
		{		
			ErrorMessage(tkFILE_NOT_OPEN);
			return S_OK;
		}
	}

	if( isEditingTable == FALSE )
	{	
		//ErrorMessage(tkDBF_NOT_IN_EDIT_MODE);		// lsu: user wants to close edit mode, it's already closed
		*retval = VARIANT_TRUE;						// so what's the problem? let's return success
		return S_OK;
	}

	if( ApplyChanges != VARIANT_FALSE )
	{		
        // checking whether the chnages to _fields were made; we need to rewrite the whole file in this case
		if (!needToSaveAsNewFile)
		{
			for(int i =0; i < (int)_fields.size(); i++)
			{
				CField* fld  = (CField*)_fields[i].field;
				if (fld->isUpdated)
				{
					needToSaveAsNewFile = true;
					break;
				}
			}
		}
		
		if (needToSaveAsNewFile)
        {
		    // generate a tmpfilename
		    char * tmpfname = new char[MAX_BUFFER];
		    char * tmppath = new char[MAX_PATH + MAX_BUFFER + 1];
		    _getcwd(tmppath,MAX_PATH);
		    tmpnam(tmpfname);
		    CString * tempFilename = new CString(tmpfname);
		    tempFiles.push_back(tempFilename);
		    // Don't free tempFilename at the end of this function,
		    // the CTableClass destructor will handle it

		    strcat( tmppath, tmpfname );
		    strcat( tmppath, ".dbf" );
            if (SaveToFile(tmppath, false, cBack))
            {
		        BOOL result = CopyFile(tmppath,filename,FALSE);
		        _unlink(tmppath);
		        delete [] tmpfname;
		        tmpfname = NULL;
		        delete [] tmppath;
		        tmppath = NULL;
            }
            else
            {
	        	ErrorMessage(tkCANT_CREATE_DBF);
            }
        }
        else //can edit the file inplace, no need to save to a temporary file
        {
			if (!SaveToFile(filename, true, cBack))
            {
	        	ErrorMessage(tkCANT_CREATE_DBF);
            }
        }
    }

	// Mark _fields as unchnaged
	for(int i = 0; i < (int)_fields.size(); i++)
	{
		CField* fld  = (CField*)_fields[i].field;
		fld->isUpdated = false;
	}

    isEditingTable = FALSE;
    return Open(A2BSTR(filename), cBack, retval);
}

// *******************************************************************
//		EditDeleteRow()
// *******************************************************************
STDMETHODIMP CTableClass::EditDeleteRow(long RowIndex, VARIANT_BOOL *retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())
	*retval = VARIANT_FALSE;

	if( isEditingTable == FALSE )
	{	
		ErrorMessage(tkDBF_NOT_IN_EDIT_MODE);
		return S_OK;
	}

    if( RowIndex < 0 || RowIndex >= RowCount() )
	{	
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		return S_OK;
	}

	// lsu on 10-nov-2009: we should delete records in place, otherwise we get wrong data with get_CellValue call
    if( _rows[RowIndex].row != NULL )
        delete _rows[RowIndex].row;
    _rows.erase(_rows.begin() + RowIndex);

	*retval = VARIANT_TRUE;
    needToSaveAsNewFile = true;
	return S_OK;
}

// *********************************************************************
//		Save()
// *********************************************************************
//Neio July 28,2009: this method is simple,but low efficiency
STDMETHODIMP CTableClass::Save(ICallback *cBack, VARIANT_BOOL *retval)
{
	StopEditingTable(VARIANT_TRUE,cBack,retval);
	StartEditingTable(cBack,retval);
	return S_OK;
}

// *********************************************************************
//		GetFieldType()
// *********************************************************************
FieldType CTableClass::GetFieldType(long fieldIndex)
{
    IField * field = NULL;
    get_Field(fieldIndex,&field);
    FieldType type;				
    field->get_Type(&type);
    field->Release();
    return type;
}

// *********************************************************************
//		GetFieldPrecision()
// *********************************************************************
long CTableClass::GetFieldPrecision(long fieldIndex)
{
    IField * field = NULL;
    get_Field(fieldIndex,&field);
    long precision;
    field->get_Precision(&precision);
    field->Release();
    return precision;
}

// *********************************************************************
//		ClearRow()
// *********************************************************************
void CTableClass::ClearRow(long rowIndex)
{
    if (rowIndex > 0 && rowIndex < RowCount())
    {
        if (_rows[rowIndex].row != 0)
        {
            delete _rows[rowIndex].row;
            _rows[rowIndex].row = NULL;
        }
    }
}

// *********************************************************************
//		get_MinValue
// *********************************************************************
STDMETHODIMP CTableClass::get_MinValue(long FieldIndex, VARIANT* retval)
{
	if( FieldIndex < 0 || FieldIndex >= (long)_fields.size())
	{
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		retval = NULL;
	}
	else	
	{
		CComVariant min, val;
		for (unsigned long i = 0; i < _rows.size(); i++)
		{
			if( ReadRecord(i) && _rows[i].row != NULL)
			val.Copy(_rows[i].row->values[FieldIndex]);
			if (i == 0)	min = val;
			else if (val < min)	min = val;
			val.Clear();
		}
		VariantCopy(retval, &min);
	}
	return S_OK;
}

// *********************************************************************
//		get_MaxValue()
// *********************************************************************
STDMETHODIMP CTableClass::get_MaxValue(long FieldIndex, VARIANT* retval)
{
	if( FieldIndex < 0 || FieldIndex >= (long)_fields.size())
	{
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		retval = NULL;
	}
	else	
	{
		CComVariant max, val;
		for (long i = 0; i < (long)_rows.size(); i++)
		{
			if( ReadRecord(i) && _rows[i].row != NULL)
			val.Copy(_rows[i].row->values[FieldIndex]);
			if (i == 0)	max = val;
			else if (val > max)	max = val;
			val.Clear();
		}
		VariantCopy(retval, &max);
	}
	return S_OK;
}

// *********************************************************************
//		get_MaxValue()
// *********************************************************************
STDMETHODIMP CTableClass::get_MeanValue(long FieldIndex, double* retval)
{
	if( FieldIndex < 0 || FieldIndex >= (long)_fields.size())
	{
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		*retval = 0.0;
	}
	else	
	{
		FieldType type;
		_fields[FieldIndex].field->get_Type(&type);
		if (type == DOUBLE_FIELD || type == INTEGER_FIELD)
		{
			double sum = 0;
			for (unsigned long i = 0; i < _rows.size(); i++)
			{
				if( ReadRecord(i) && _rows[i].row != NULL)
				{
					if (type == DOUBLE_FIELD)	sum += _rows[i].row->values[FieldIndex]->dblVal;
					else						sum += _rows[i].row->values[FieldIndex]->lVal;
				}
			}
			*retval = sum/(double)_rows.size();
		}
		else
		{
			//ErrorMessage(InvalidFieldType);
			*retval = 0.0;
		}
	}
	return S_OK;
}

// *********************************************************************
//		get_MaxValue()
// *********************************************************************
STDMETHODIMP CTableClass::get_StandardDeviation(long FieldIndex, double* retval)
{
	if( FieldIndex < 0 || FieldIndex >= (long)_fields.size())
	{
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
		retval = NULL;
	}
	else	
	{
		FieldType type;
		_fields[FieldIndex].field->get_Type(&type);
		if (type == DOUBLE_FIELD || type == INTEGER_FIELD)
		{
			double mean;
			get_MeanValue(FieldIndex, &mean);
			double std = 0.0;
			for (unsigned long i = 0; i < _rows.size(); i++)
			{
				if( ReadRecord(i) && _rows[i].row != NULL)
				{
					if (type == DOUBLE_FIELD)	std += pow(_rows[i].row->values[FieldIndex]->dblVal - mean, 2);
					else						std += pow((double)_rows[i].row->values[FieldIndex]->lVal - mean, 2);
				}
			}
			*retval = sqrt(std/ (_rows.size() - 1));
		}
		else
		{
			//ErrorMessage(InvalidFieldType);
			*retval = 0.0;
		}
	}
	return S_OK;
}

inline void CTableClass::ErrorMessage(long ErrorCode)
{
	lastErrorCode = ErrorCode;
	if( globalCallback != NULL) globalCallback->Error(OLE2BSTR(key),A2BSTR(ErrorMsg(lastErrorCode)));
	return;
}

// *****************************************************************
//			GenerateCategories()
// *****************************************************************
// Overload for passing empty range
vector<CategoriesData>* CTableClass::GenerateCategories(long FieldIndex, tkClassificationType ClassificationType, long numClasses)
{
	CComVariant minVal, maxVal;
	minVal.vt = VT_EMPTY;
	maxVal.vt = VT_EMPTY;
	return GenerateCategories(FieldIndex, ClassificationType, numClasses, minVal, maxVal);
}

// *****************************************************************
//			GenerateCategories()
// *****************************************************************
vector<CategoriesData>* CTableClass::GenerateCategories(long FieldIndex, tkClassificationType ClassificationType, long numClasses, 
														 CComVariant minValue, CComVariant maxValue)
{
	if( FieldIndex < 0 || FieldIndex >= this->FieldCount())
	{
		ErrorMessage(tkINDEX_OUT_OF_BOUNDS); 
		return S_OK;
	} 
	
	long numShapes = this->RowCount();

	// getting field type
	IField* fld = NULL;
	this->get_Field(FieldIndex, &fld);
	FieldType fieldType;
	fld->get_Type(&fieldType);
	BSTR str;
	fld->get_Name(&str);
	USES_CONVERSION;
	CString fieldName = OLE2CA(str);
	SysFreeString(str);
	fld->Release(); fld = NULL;
	
	/* we won't define intervals for string values */
	if (ClassificationType != ctUniqueValues && fieldType == STRING_FIELD)
	{
		ErrorMessage(tkINVALID_PARAMETER_VALUE); 
		return S_OK;
	}
	
	if ((numClasses <= 0 || numClasses > 1000) && (ClassificationType != ctUniqueValues))
	{
		ErrorMessage(tkINVALID_PARAMETER_VALUE); 
		return S_OK;
	}
	
	// natural breaks aren't designed to work otherwise
	if (numShapes < numClasses && ClassificationType == ctNaturalBreaks)
	{
		numClasses = numShapes;
	}
	
	// values in specified range should be classified
	bool useRange = minValue.vt != VT_EMPTY && maxValue.vt != VT_EMPTY && fieldType == DOUBLE_FIELD;

	if (useRange) //fieldType == DOUBLE_FIELD)
	{
		double max, min;
		dVal(minValue, min);
		dVal(maxValue, max);
		minValue.vt = VT_R8;
		maxValue.vt = VT_R8;
		minValue.dblVal = min;
		maxValue.dblVal = max;
	}

	//bool useRange = minValue.vt == VT_R8 && maxValue.vt == VT_R8 && fieldType != STRING_FIELD;

	std::vector<CategoriesData>* result = new std::vector<CategoriesData>;
	if (ClassificationType == ctUniqueValues)
	{
		std::set<CComVariant> dict;
		CComVariant val;

		for(long i = 0; i< numShapes; i++)
		{
			this->get_CellValue(FieldIndex, i, &val);
			if (useRange && (val.dblVal < minValue.dblVal || val.dblVal > maxValue.dblVal))
				continue;

			if (dict.find(val) == dict.end())
					dict.insert(val);
		}
		/* creating categories */
		std::vector<CComVariant> values;
		copy(dict.begin(), dict.end(), inserter(values, values.end()));
		
		for (int i = 0; i < (int)values.size(); i++ )
		{
			CategoriesData data;
			data.minValue = values[i];
			data.maxValue = values[i];
			result->push_back(data);
		}
		dict.clear();
		values.clear();
	}
	else if (ClassificationType == ctEqualSumOfValues)
	{
		CComVariant val;
		
		// sorting the values
		std::vector<double> values;
		double totalSum = 0, dValue;
		for(int i=0; i< numShapes; i++)
		{
			this->get_CellValue(FieldIndex, i, &val);
			
			if (useRange && (val.dblVal < minValue.dblVal || val.dblVal > maxValue.dblVal))
				continue;

			dVal(val, dValue); val.Clear();
			values.push_back(dValue);
			totalSum += dValue;
		}
		sort(values.begin(), values.end());
		
		double step = totalSum / (double)numClasses;
		int index = 1; 
		double sum = 0;
		
		for (int i = 0; i < (int)values.size(); i++ )
		{
			sum += values[i];
			if (sum >= step * (double)index || i == numShapes - 1)	
			{
				CategoriesData data;
				
				if ( index == numClasses )
					data.maxValue = values[values.size() - 1];
				else if ( i != 0 )
					data.maxValue = (values[i] + values[i-1])/2;
				else
					data.maxValue = values[0];

				if (index == 1) 
					data.minValue = values[0];
				else			
					data.minValue = (*result)[result->size() - 1].maxValue;
				
				result->push_back(data);
				index++;
			}
		}
	}
	else if (ClassificationType == ctEqualIntervals)
	{
		CComVariant vMin, vMax;
		
		if (useRange)
		{
			vMin = minValue;
			vMax = maxValue;
		}
		else
		{
			this->get_MinValue(FieldIndex, &vMin);
			this->get_MaxValue(FieldIndex, &vMax);
		}
		
		double dMin, dMax;
		dVal(vMin, dMin); dVal(vMax, dMax);
		vMin.Clear(); vMax.Clear();
		
		/*	creating classes */
		double dStep = (dMax - dMin)/(double)numClasses;
		while (dMin < dMax)
		{
			CategoriesData data;
			data.minValue = dMin;
			data.maxValue = dMin + dStep;
			result->push_back(data);

			dMin += dStep;
		}
	}
	else if (ClassificationType == ctEqualCount)
	{
		CComVariant vMin, vMax;
		if (useRange)
		{
			vMin = minValue;
			vMax = maxValue;
		}
		else
		{
			this->get_MinValue(FieldIndex, &vMin);
			this->get_MaxValue(FieldIndex, &vMax);
		}
		
		double dMin, dMax;
		dVal(vMin, dMin); dVal(vMax, dMax);
		vMin.Clear(); vMax.Clear();
		
		// sorting the values
		std::vector<double> values;
		for(int i=0; i< numShapes; i++)
		{
			this->get_CellValue(FieldIndex, i, &vMin);
			dVal(vMin, dMin); vMin.Clear();
			values.push_back(dMin);
		}
		sort(values.begin(), values.end());

		/*	creating classes */
		int i = 0;
		int count = numShapes/numClasses;
		
		for (int i = 0; i < numShapes; i += count)
		{
			dMin = values[i];
			if (i + count < numShapes)
				dMax = values[i + count];
			else
				dMax = values[numShapes - 1];

			CategoriesData data;
			data.minValue = dMin;
			data.maxValue = dMax;
			result->push_back(data);
		}
		values.clear();
	}
	else if (ClassificationType == ctNaturalBreaks)
	{
		CComVariant vMin; double dMin;
		// sorting the values
		std::vector<double> values;
		for(int i=0; i< numShapes; i++)
		{
			this->get_CellValue(FieldIndex, i, &vMin);
			if (useRange && (vMin.dblVal < minValue.dblVal || vMin.dblVal > maxValue.dblVal))
				continue;

			dVal(vMin, dMin); vMin.Clear();
			values.push_back(dMin);
		}
		sort(values.begin(), values.end());

		CJenksBreaks breaks(&values, numClasses);
		if (breaks.Initialized())
		{
			breaks.Optimize();
			std::vector<long>* startIndices = breaks.get_Results();
			//std::vector<int>* startIndices = breaks.TestIt(&values, numClasses);

			if (startIndices)
			{
				for (unsigned int i = 0; i < startIndices->size(); i++)
				{
					CategoriesData data;
					data.minValue = values[(*startIndices)[i]];
					if (i == startIndices->size() - 1)
						data.maxValue = values[values.size() - 1];
					else
						data.maxValue = values[(*startIndices)[i+1]];
					
					result->push_back(data);
				}
				delete startIndices;
			}
		}
	}
	else if (ClassificationType == ctStandardDeviation)
	{
		double mean, sdo;
		this->get_MeanValue(FieldIndex, &mean);
		this->get_StandardDeviation(FieldIndex, &sdo);
		
		for (int i = -3; i <= 2; i++ )
		{
			CategoriesData data;
			data.minValue = mean + sdo * i;
			data.maxValue = mean + sdo * (i + 1);
			result->push_back(data);
		}
	}

	// ------------------------------------------------------
	//		generating text expressions
	// ------------------------------------------------------
	if (ClassificationType == ctUniqueValues)
	{
		for (int i = 0; i < (int)result->size(); i++ )
		{
			//CString strExpression;
			CString strValue;
			CComVariant* val = &(*result)[i].minValue;
			switch (val->vt)
			{
				case VT_BSTR:	
								strValue = OLE2CA(val->bstrVal);
								(*result)[i].name = strValue;
								(*result)[i].expression = "[" + fieldName + "] = \"" + strValue + "\""; 
								break;
				case VT_R8:		
								strValue.Format("%g", val->dblVal);
								(*result)[i].name = strValue;
								(*result)[i].expression = "[" + fieldName + "] = " + strValue;
								break;
				case VT_I4:		
								strValue.Format("%i", val->lVal);
								(*result)[i].name = strValue;
								(*result)[i].expression = "[" + fieldName + "] = " + strValue;
								break;
			}
		}
	}
	else //if (ClassificationType == ctEqualIntervals || ClassificationType == ctEqualCount)
	{
		// in case % is present, we need to put to double it for proper formatting
		fieldName.Replace("%", "%%");

		for (int i = 0; i < (int)result->size(); i++ )
		{
			CategoriesData* data = &((*result)[i]);

			CString strExpression, strName, sFormat;

			if (i == 0)
			{
				data->minValue.dblVal = floor(data->minValue.dblVal);
			}
			else if(i == result->size() - 1)
			{
				data->maxValue.dblVal = ceil(data->maxValue.dblVal);
			}
			
			CString upperBound = (i == result->size() - 1) ? "<=" : "<";

			switch (data->minValue.vt)
			{
				case VT_R8:		
								sFormat = "%g";
								data->name = Utility::FormatNumber(data->minValue.dblVal, sFormat) + " - " + Utility::FormatNumber(data->maxValue.dblVal, sFormat);
								data->expression.Format("[" + fieldName + "] >= %f AND [" + fieldName + "] " + upperBound  + " %f", data->minValue.dblVal, data->maxValue.dblVal);
								break;
				case VT_I4:		
								sFormat = "%i";
								data->name = Utility::FormatNumber(data->minValue.dblVal, sFormat) + " - " + Utility::FormatNumber(data->maxValue.dblVal, sFormat);
								data->expression.Format("[" + fieldName + "] >= %i AND [" + fieldName + "] "  + upperBound + " %i", data->minValue.lVal, data->maxValue.lVal);
								break;
			}
		}
	}

	if (result->size() > 0)
	{
		return result;
	}
	else
	{
		delete result;
		return NULL;
	}
}

// *****************************************************************
//		FieldNames()
// *****************************************************************
std::vector<CString>* CTableClass::get_FieldNames()
{
	std::vector<CString>* names = new std::vector<CString>;
	for (unsigned  int i = 0; i < _fields.size(); i++ )
	{
		BSTR s;
		_fields[i].field->get_Name(&s);
		USES_CONVERSION;
		CString str = OLE2CA(s);
		SysFreeString(s);
		names->push_back(str);
	}
	return names;
}

// *****************************************************************
//		FieldIndexByName()
// *****************************************************************
STDMETHODIMP CTableClass::get_FieldIndexByName(BSTR FieldName, long* retval)
{
	AFX_MANAGE_STATE(AfxGetStaticModuleState())

	*retval = -1;
	USES_CONVERSION;
	CString searchName = OLE2CA(FieldName);
	for (unsigned int i = 0; i < _fields.size(); i++)
	{	
		IField* field =_fields[i].field;
		if (field)
		{
			BSTR name;
			field->get_Name(&name);
			if (_stricmp(OLE2CA(name), searchName.GetString()) == 0)
			{
				*retval = i;
				break;
			}
			SysFreeString(name);
		}
	}

	return S_OK;
}

// *****************************************************************
//		get_FieldValues()
// *****************************************************************
bool CTableClass::get_FieldValuesDouble(int FieldIndex, std::vector<double>& values)
{
	values.clear();

	if( FieldIndex < 0 || FieldIndex >= FieldCount())
	{	
		return false;
	}
	
	FieldType type;
	_fields[FieldIndex].field->get_Type(&type);
	
	int index = _fields[FieldIndex].oldIndex;	// real index of the field

	values.reserve(_rows.size());
	for (unsigned int i = 0; i < _rows.size(); i++)
	{
		if (_rows[i].row)
		{
			VARIANT* var = _rows[i].row->values[FieldIndex];
			if (var && var->vt != VT_NULL)
			{
				values.push_back(var->dblVal);
			}
			else
			{
				values.push_back(0.0);
			}
		}
		else
		{
			values.push_back(DBFReadDoubleAttribute(dbfHandle,_rows[i].oldIndex,index));
		}
	}
	return true;
}

// *****************************************************************
//		get_FieldValues()
// *****************************************************************
bool CTableClass::get_FieldValuesInteger(int FieldIndex, std::vector<int>& values)
{
	values.clear();

	if( FieldIndex < 0 || FieldIndex >= FieldCount())
	{	
		return false;
	}
	
	FieldType type;
	_fields[FieldIndex].field->get_Type(&type);
	
	int index = _fields[FieldIndex].oldIndex;	// real index of the field

	values.reserve(_rows.size());
	for (unsigned int i = 0; i < _rows.size(); i++)
	{
		if (_rows[i].row)
		{
			VARIANT* var = _rows[i].row->values[FieldIndex];
			if (var && var->vt != VT_NULL)
			{
				values.push_back(var->lVal);
			}
			else
			{
				values.push_back(0);
			}
		}
		else
		{
			values.push_back(DBFReadIntegerAttribute(dbfHandle,_rows[i].oldIndex,index));
		}
	}
	return true;
}

// *****************************************************************
//		get_FieldValues()
// *****************************************************************
bool CTableClass::get_FieldValuesString(int FieldIndex, std::vector<CString>& values)
{
	USES_CONVERSION;

	values.clear();

	if( FieldIndex < 0 || FieldIndex >= FieldCount())
	{	
		return false;
	}
	
	FieldType type;
	_fields[FieldIndex].field->get_Type(&type);
	
	int index = _fields[FieldIndex].oldIndex;	// real index of the field
	
	values.reserve(_rows.size());
	for (unsigned int i = 0; i < _rows.size(); i++)
	{
		if (_rows[i].row)
		{
			VARIANT* var = _rows[i].row->values[FieldIndex];
			if (var && var->vt != VT_NULL)
			{
				
				values.push_back( OLE2CA(var->bstrVal));
			}
			else
			{
				values.push_back("");
			}
		}
		else
		{
			values.push_back(DBFReadStringAttribute(dbfHandle,_rows[i].oldIndex,index));
		}
	}
	return true;
}

// *****************************************************************
//		set_IndexValue()
// *****************************************************************
bool CTableClass::set_IndexValue(int rowIndex)
{
	if (!isEditingTable)
		return false;
	
	if (rowIndex < 0 || rowIndex > (int)_rows.size())
		return false;

	long fieldIndex = -1;
	BSTR fieldName = A2BSTR("MWShapeID");
	this->get_FieldIndexByName(fieldName, &fieldIndex);
	if (fieldIndex == -1)
		return false;

	IField* field = NULL;
	this->get_Field(fieldIndex, &field);
	FieldType type;
	field->get_Type(&type);
	field->Release();

	if (type != INTEGER_FIELD)
		return false;

	// there is no cached value;  a full search is needed
	CComVariant val;
	if (m_maxRowId == -1)
	{
		for (unsigned int i = 0; i < _rows.size(); i++)
		{
			if (i != rowIndex)
			{
				this->get_CellValue(fieldIndex, i, &val);
				long value;
				lVal(val, value);
				if (value > m_maxRowId)
					m_maxRowId = value;
			}
		}
	}
	
	// setting the value
	val.vt = VT_I4;
	val.lVal = m_maxRowId + 1;
	VARIANT_BOOL vbretval;
	this->EditCellValue(fieldIndex, rowIndex, val, &vbretval);
	if (vbretval)
	{
		m_maxRowId++;
		
	}
	return vbretval == VARIANT_FALSE ? false : true;
}

// *****************************************************************
//		CalculateStat()
// *****************************************************************
//STDMETHODIMP CTableClass::CalculateStat(LONG FieldIndex, tkGroupOperation Statistic, BSTR Expression, VARIANT* Result, VARIANT_BOOL* retVal)
//{
//	AFX_MANAGE_STATE(AfxGetStaticModuleState());
//	*retVal = VARIANT_FALSE;
//
//	if( FieldIndex < 0 || FieldIndex >= (long)_fields.size())
//	{
//		ErrorMessage(tkINDEX_OUT_OF_BOUNDS);
//		return S_OK;
//	}
//
//	USES_CONVERSION;
//	CString expr =  OLE2CA(Expression);
//	CString errString;
//	std::vector<long> indices;
//	if (expr != "")
//	{
//		if (!this->Query_(Expression, indices, errString))
//		{
//			ErrorMessage(tkINVALID_EXPRESSION);
//			return S_OK;
//		}
//	}
//	else
//	{
//		for (long i = 0; i < RowCount(); i++)
//		{
//			indices.push_back(i);
//		}
//	}
//
//	CComVariant var;
//	CComVariant min, max, sum;
//	switch(Statistic)
//	{
//		case operMin:
//			for (unsigned int i = 0; i < indices.size(); i++)
//			{
//				if (i == 0)
//				{
//					this->get_CellValue(FieldIndex, indices[i], &min);
//				}
//				else
//				{
//					this->get_CellValue(FieldIndex, indices[i], &var);
//					if (min > var)
//					{
//						min = var;
//					}
//				}
//			}
//			var = min;
//			break;
//		case operMax:
//			for (unsigned int i = 0; i < indices.size(); i++)
//			{
//				if (i == 0)
//				{
//					this->get_CellValue(FieldIndex, indices[i], &max);
//				}
//				else
//				{
//					this->get_CellValue(FieldIndex, indices[i], &var);
//					if (max < var)
//					{
//						max = var;
//					}
//				}
//			}
//			var = max;
//			break;
//		case operCount:
//			var = indices.size();
//			break;
//		case operAverage:
//		case operSum:
//			var = "Unsupported";
//			
//			// TODO: write type specific code
//			/*if (_fields[FieldIndex].field )
//			{
//				FieldType type;
//				_fields[FieldIndex].field->get_Type(&type);
//				if (type == STRING_FIELD)
//				{
//					var = "Unsupported";
//				}
//				else
//				{
//					for (unsigned int i = 0; i < indices.size(); i++)
//					{
//						this->get_CellValue(FieldIndex, indices[i], &var);
//						sum = sum + var;
//					}
//					
//					if (operAverage)
//					{
//						var = sum/indices.size();
//					}
//					else
//					{
//						var = sum;
//					}
//				}
//			}*/
//			break;
//	}
//
//	*Result = var;
//	
//	*retVal = VARIANT_TRUE;
//	return S_OK;
//}
