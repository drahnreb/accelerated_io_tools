/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2008-2016 SciDB, Inc.
* All Rights Reserved.
*
* accelerated_io_tools is a plugin for SciDB, an Open Source Array DBMS maintained
* by Paradigm4. See http://www.paradigm4.com/
*
* accelerated_io_tools is free software: you can redistribute it and/or modify
* it under the terms of the AFFERO GNU General Public License as published by
* the Free Software Foundation.
*
* accelerated_io_tools is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the AFFERO GNU General Public License for the complete license terms.
*
* You should have received a copy of the AFFERO GNU General Public License
* along with accelerated_io_tools.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
*
* END_COPYRIGHT
*/

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <query/Operator.h>
#include <util/PathUtils.h>

#ifndef AIO_SAVE_SETTINGS
#define AIO_SAVE_SETTINGS

using boost::algorithm::trim;
using boost::starts_with;
using boost::lexical_cast;
using boost::bad_lexical_cast;
using namespace std;

namespace scidb
{

class AioSaveSettings
{
public:
    static size_t chunkDataOffset()
    {
        return (sizeof(ConstRLEPayload::Header) + 2 * sizeof(ConstRLEPayload::Segment) + sizeof(varpart_offset_t) + 5);
    }

    static size_t chunkSizeOffset()
    {
        return (sizeof(ConstRLEPayload::Header) + 2 * sizeof(ConstRLEPayload::Segment) + sizeof(varpart_offset_t) + 1);
    }


private:
    enum FormatType
    {
        TEXT   = 0,
        BINARY = 1,
        ARROW  = 2
    };

    size_t                      _bufferSize;
    int64_t                     _cellsPerChunk;
    char                        _attributeDelimiter;
    char                        _lineDelimiter;
    map<InstanceID, string>     _instancesAndPaths;
    size_t const                _numInstances;
    FormatType                  _format;
    string                      _binaryFormatString;
    string                      _nullPrefix;
    bool                        _printNullCode;
    string                      _nullPostfix;
    bool                        _printCoordinates;
    bool                        _quoteStrings;
    bool                        _writeHeader;
    int32_t                     _precision;
    bool                        _attsOnly;
    int64_t                     _resultSizeLimit;

public:
    static const size_t MAX_PARAMETERS = 6;

    AioSaveSettings(vector<shared_ptr<OperatorParam> > const& operatorParameters,
                    bool logical,
                    shared_ptr<Query>& query):
                _bufferSize(8 * 1024 * 1024),
                _cellsPerChunk(-1),
                _attributeDelimiter('\t'),
                _lineDelimiter('\n'),
                _numInstances(query->getInstancesCount()),
                _format(TEXT),
                _binaryFormatString(""),
                _nullPrefix("\\N"),
                _printNullCode(false),
                _nullPostfix(""),
                _printCoordinates(false),
                _quoteStrings(false),
                _writeHeader(false),
                _precision(std::numeric_limits<double>::digits10),
                _attsOnly(true),
                _resultSizeLimit(-1)
    {
        string const bufferSizeHeader              = "buffer_size=";
        string const cellsPerChunkHeader           = "cells_per_chunk=";
        string const attributeDelimiterHeader      = "attribute_delimiter=";
        string const lineDelimiterHeader           = "line_delimiter=";
        string const formatHeader                  = "format=";
        string const filePathHeader                = "path=";
        string const filePathsHeader               = "paths=";
        string const instanceHeader                = "instance=";
        string const instancesHeader               = "instances=";
        string const nullPatternHeader             = "null_pattern=";
        string const precisionHeader               = "precision=";
        string const attsOnlyHeader                = "atts_only=";
        string const resultSizeLimitHeader         = "result_size_limit=";
        size_t const nParams = operatorParameters.size();
        bool  cellsPerChunkSet      = false;
        bool  bufferSizeSet         = false;
        bool  attributeDelimiterSet = false;
        bool  lineDelimiterSet      = false;
        bool  formatSet             = false;
        bool  nullPatternSet        = false;
        bool  resultSizeLimitSet    = false;
        bool  usingCsvPlus          = false;
        if(_precision <= 0)
        {//correct for an unfortunate configuration problem that may arise
            _precision = 6;
        }
        bool  precisionSet          = false;
        bool  attsOnlySet           = false;
        vector<string>     filePaths;
        vector<InstanceID> instanceIds;
        if (nParams > MAX_PARAMETERS)
        {   //assert-like exception. Caller should have taken care of this!
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal number of parameters passed to UnparseSettings";
        }
        for (size_t i= 0; i<nParams; ++i)
        {
            shared_ptr<OperatorParam>const& param = operatorParameters[i];
            string parameterString;
            if (logical)
            {
                parameterString = evaluate(((shared_ptr<OperatorParamLogicalExpression>&) param)->getExpression(), TID_STRING).getString();
            }
            else
            {
                parameterString = ((shared_ptr<OperatorParamPhysicalExpression>&) param)->getExpression()->evaluate().getString();
            }
            if (starts_with(parameterString, cellsPerChunkHeader))
            {
                if (cellsPerChunkSet)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set cells_per_chunk multiple times";
                }
                string paramContent = parameterString.substr(cellsPerChunkHeader.size());
                trim(paramContent);
                try
                {
                    _cellsPerChunk = lexical_cast<int64_t>(paramContent);
                    if(_cellsPerChunk <= 0)
                    {
                        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "cells_per_chunk must be positive";
                    }
                }
                catch (bad_lexical_cast const& exn)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "could not parse cells_per_chunk";
                }
                cellsPerChunkSet = true;
            }
            else if (starts_with(parameterString, bufferSizeHeader))
            {
                if (bufferSizeSet)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set buffer_size multiple times";
                }
                string paramContent = parameterString.substr(bufferSizeHeader.size());
                trim(paramContent);
                try
                {
                    _bufferSize = lexical_cast<size_t>(paramContent);
                    size_t const bufferSizeLimit = chunkDataOffset() + 8;
                    if(_bufferSize <= bufferSizeLimit)
                    {
                        ostringstream err;
                        err << "buffer_size must be above " << bufferSizeLimit;
                        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << err.str();
                    }
                }
                catch (bad_lexical_cast const& exn)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "could not parse buffer_size";
                }
                bufferSizeSet = true;
            }             //some day I'll make this function smaller, I swear!
            else if (starts_with(parameterString, attributeDelimiterHeader))
            {
                if (attributeDelimiterSet)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set attribute_delimiter multiple times";
                }
                string paramContent = parameterString.substr(attributeDelimiterHeader.size());
                trim(paramContent);
                if (paramContent == "\\t")
                {
                    _attributeDelimiter = '\t';
                }
                else if (paramContent == "\\r")
                {
                    _attributeDelimiter = '\r';
                }
                else if (paramContent == "\\n")
                {
                    _attributeDelimiter = '\n';
                }
                else if (paramContent == "")
                {
                    _attributeDelimiter = ' ';
                }
                else
                {
                    try
                    {
                        _attributeDelimiter = lexical_cast<char>(paramContent);
                    }
                    catch (bad_lexical_cast const& exn)
                    {
                        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "could not parse attribute_delimiter";
                    }
                }
                attributeDelimiterSet = true;
            }
            else if (starts_with (parameterString, lineDelimiterHeader))
            {
                if(lineDelimiterSet)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set line_delimiter multiple times";
                }
                string paramContent = parameterString.substr(lineDelimiterHeader.size());
                trim(paramContent);
                if (paramContent == "\\t")
                {
                    _lineDelimiter = '\t';
                }
                else if (paramContent == "\\r")
                {
                    _lineDelimiter = '\r';
                }
                else if (paramContent == "\\n")
                {
                    _lineDelimiter = '\n';
                }
                else if (paramContent == "")
                {
                    _lineDelimiter = ' ';
                }
                else
                {
                    try
                    {
                        _lineDelimiter = lexical_cast<char>(paramContent);
                    }
                    catch (bad_lexical_cast const& exn)
                    {
                        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "could not parse line_delimiter";
                    }
                }
                lineDelimiterSet = true;
            }
            else if (starts_with (parameterString, formatHeader))
            {
                if(formatSet)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set format multiple times";
                }
                string paramContent = parameterString.substr(formatHeader.size());
                trim(paramContent);
                if(paramContent == "tdv" || paramContent == "tsv" || paramContent == "csv+" || paramContent == "lcsv+")
                {
                    _format = TEXT;
                    if(paramContent == "csv+" || paramContent == "lcsv+")
                    {
                        usingCsvPlus = true;
                    }
                }
                else if(paramContent == "arrow")
                {
                    _format = ARROW;
                }
                else
                {
                    _format = BINARY;
                    if(paramContent[0]!='(' || paramContent[paramContent.size()-1] != ')')
                    {
                        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "format must be either 'tdv', 'tsv', 'csv+', 'arrow', or a binary spec such as '(int64,double,string null)'";
                    }
                    _binaryFormatString = paramContent;
                }
                formatSet = true;
            }
            else if (starts_with (parameterString, filePathHeader))
            {
                if(filePaths.size())
                {
                   throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set path multiple times";
                }
                string paramContent = parameterString.substr(filePathHeader.size());
                trim(paramContent);
                filePaths.push_back(path::expandForSave(paramContent, *query));
            }
            else if (starts_with (parameterString, filePathsHeader))
            {
                if(filePaths.size())
                {
                   throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set paths multiple times";
                }
                string paramContent = parameterString.substr(filePathsHeader.size());
                trim(paramContent);
                stringstream ss(paramContent);
                string tok;
                while(getline(ss, tok, ';'))
                {
                    if(tok.size() != 0)
                    {
                        filePaths.push_back(path::expandForSave(tok, *query));
                    }
                }
            }
            else if (starts_with (parameterString, instanceHeader))
            {
                if(instanceIds.size())
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set instance multiple times";
                }
                string paramContent = parameterString.substr(instanceHeader.size());
                trim(paramContent);
                try
                {
                    int64_t iid = lexical_cast<int64_t>(paramContent);
                    instanceIds.push_back( iid);
                }
                catch (bad_lexical_cast const& exn)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "could not parse instance id";
                }
            }
            else if (starts_with (parameterString, instancesHeader))
            {
                if(instanceIds.size())
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set instances multiple times";
                }
                string paramContent = parameterString.substr(instancesHeader.size());
                trim(paramContent);
                stringstream ss(paramContent);
                string tok;
                while(getline(ss, tok, ';'))
                {
                    try
                    {
                        int64_t iid = lexical_cast<int64_t>(tok);
                        instanceIds.push_back(iid);
                    }
                    catch (bad_lexical_cast const& exn)
                    {
                        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "could not parse instance id";
                    }
                }
            }
            else if (starts_with(parameterString, nullPatternHeader))
            {
                if(nullPatternSet)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set null_pattern multiple times";
                }
                string nullPattern = parameterString.substr(nullPatternHeader.size());
                _nullPrefix.resize(0);
                _nullPostfix.resize(0);
                _printNullCode = false;
                size_t c;
                for(c=0; c<nullPattern.size(); ++c)
                {
                    if(nullPattern[c] != '%')
                    {
                        _nullPrefix.append(1, nullPattern[c]);
                    }
                    else
                    {
                        break;
                    }
                }
                if(c<nullPattern.size() && nullPattern[c]=='%')
                {
                    _printNullCode = true;
                    ++c;
                }
                for( ; c<nullPattern.size(); ++c)
                {
                    _nullPostfix.append(1, nullPattern[c]);
                }
                nullPatternSet = true;
            }
            else if (starts_with(parameterString, precisionHeader))
            {
                if (precisionSet)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set precision multiple times";
                }
                string paramContent = parameterString.substr(precisionHeader.size());
                trim(paramContent);
                try
                {
                    _precision = lexical_cast<int32_t>(paramContent);
                    if(_precision<=0)
                    {
                        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "precision must be positive";
                    }
                }
                catch (bad_lexical_cast const& exn)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "could not parse precision";
                }
                precisionSet = true;
            }
            else if (starts_with(parameterString, attsOnlyHeader))
            {
                if (attsOnlySet)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set atts_only multiple times";
                }
                string paramContent = parameterString.substr(attsOnlyHeader.size());
                trim(paramContent);
                try
                {
                    _attsOnly = lexical_cast<bool>(paramContent);
                }
                catch (bad_lexical_cast const& exn)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "could not parse atts_only";
                }
                attsOnlySet = true;
            } else if (starts_with(parameterString, resultSizeLimitHeader))
            {
                if (resultSizeLimitSet)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "illegal attempt to set file_limit multiple times";
                }
                string paramContent = parameterString.substr(resultSizeLimitHeader.size());
                trim(paramContent);
                try
                {
                    _resultSizeLimit = lexical_cast<int64_t>(paramContent);
                    if(_resultSizeLimit < 0)
                    {
                        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "file_limit must be positive";
                    }
                }
                catch (bad_lexical_cast const& exn)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "could not parse file_limit";
                }
                resultSizeLimitSet = true;
            }
            else
            {
                string path = parameterString;
                trim(path);
                bool containsStrangeCharacters = false;
                for(size_t i=0; i<path.size(); ++i)
                {
                   if(path[i] == '=' || path[i] == ' ' || path[i] == ';')
                   {
                       containsStrangeCharacters=true;
                       break;
                   }
                }
                if (filePaths.size() != 0 || containsStrangeCharacters)
                {
                  ostringstream errorMsg;
                  errorMsg << "unrecognized parameter: "<< parameterString;
                  throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << errorMsg.str().c_str();
                }
                filePaths.push_back(path::expandForSave(path, *query));
            }
        }
        if(filePaths.size() == 0)
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "file path(s) was not provided, or failed to parse";
        }
        if(instanceIds.size() == 0)
        {
            instanceIds.push_back(query->getPhysicalCoordinatorID(true));
        }
        if(filePaths.size() != instanceIds.size())
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "the number of file paths provided does not match the number of instance IDs";
        }
        set<InstanceID> uniqueInstances;
        for(size_t i =0; i<instanceIds.size(); ++i)
        {
            uniqueInstances.insert(instanceIds[i]);
        }
        if(uniqueInstances.size() < instanceIds.size())
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "the provided instance IDs are not unique";
        }
        for(size_t i=0; i< filePaths.size(); ++i)
        {
            InstanceID const iid = instanceIds[i];
            if(query->isPhysicalInstanceDead(iid))
            {
                ostringstream err;
                err<<"Instance "<<iid<<" is not currently part of the cluster";
                throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << err.str().c_str();
            }
            InstanceID logId = query->mapPhysicalToLogical(iid);
            _instancesAndPaths[logId] = filePaths[i];
        }
        if((_format == BINARY || usingCsvPlus) && (lineDelimiterSet || attributeDelimiterSet || nullPatternSet))
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "attribute_delimiter, line_delimiter and null_pattern are only used with 'format=tdv'";
        }
        if(usingCsvPlus)
        {
            _nullPrefix = "null";
            _quoteStrings = true;
            _printCoordinates=true;
            _attributeDelimiter = ',';
            _lineDelimiter = '\n';
            _writeHeader = true;
        }
    }

    int64_t getCellsPerChunk() const
    {
        return _cellsPerChunk;
    }

    size_t getBufferSize() const
    {
        return _bufferSize;
    }

    char getAttributeDelimiter() const
    {
        return _attributeDelimiter;
    }

    char getLineDelimiter() const
    {
        return _lineDelimiter;
    }

    bool isBinaryFormat() const
    {
        return _format == BINARY;
    }

    bool isArrowFormat() const
    {
        return _format == ARROW;
    }

    bool isAttsOnly() const
    {
        return _attsOnly;
    }

    string const& getBinaryFormatString() const
    {
        return _binaryFormatString;
    }

    map<InstanceID, string> const& getInstanceMap() const
    {
        return _instancesAndPaths;
    }

    inline void printNull(ostringstream& output, int8_t missingReason) const
    {
        output<<_nullPrefix;
        if(_printNullCode)
        {
            output<<(int64_t)missingReason<<_nullPostfix;
        }
        else
        {
            return;
        }
    }

    bool printCoordinates() const
    {
        return _printCoordinates;
    }

    bool quoteStrings() const
    {
        return _quoteStrings;
    }

    bool printHeader() const
    {
        return _writeHeader;
    }

    int32_t getPrecision() const
    {
        return _precision;
    }

    int64_t getResultSizeLimit() const
    {
        int64_t retVal = _resultSizeLimit;
        if (_resultSizeLimit > -1) {
            retVal = retVal * 1024 * 1024;
        }
        return retVal;
    }

};

}


#endif //UnparseSettings
