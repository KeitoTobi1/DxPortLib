/*
  DxPortLib - A portability library for DxLib-based software.
  Copyright (C) 2013-2014 Patrick McCarthy <mauve@sandwich.net>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.
  
  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:
    
  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required. 
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
 */

#include "PLInternal.h"

/* PL_File code doesn't actually open any files. Instead it just makes
 * a framework for hooking your own file functions in.
 */

typedef struct _FileHandle {
    const PL_FileFunctions *functions;
    void *userdata;
} FileHandle;

static PLFileOpenFileFunction s_openReadFunction = NULL;

/* ------------------------------------------------------- Memory handle */
typedef struct _MemoryHandleData {
    void *data;
    int pos;
    int length;
    
    int freeOnClose;
} MemoryHandleData;

static long long MemoryHandle_GetSize(void *userdata) {
    MemoryHandleData *mem = (MemoryHandleData *)userdata;
    return mem->length;
}
static long long MemoryHandle_Tell(void *userdata) {
    MemoryHandleData *mem = (MemoryHandleData *)userdata;
    return mem->pos;
}
static int MemoryHandle_Seek(void *userdata, long long position, int origin) {
    MemoryHandleData *mem = (MemoryHandleData *)userdata;
    switch(origin) {
        case 0: break;
        case 1: position += mem->pos; break;
        case 2: position += mem->length; break;
    }
    if (position < 0) {
        position = 0;
    }
    if (position > mem->length) {
        position = mem->length;
    }
    mem->pos = position;
    
    return 0;
}
static int MemoryHandle_Read(void *userdata, void *data, int size) {
    MemoryHandleData *mem = (MemoryHandleData *)userdata;
    int end = mem->pos + size;
    int amount;
    if (end < mem->pos) {
        return 0;
    }
    if (end > mem->length) {
        end = mem->length;
    }
    amount = end - mem->pos;
    memcpy(data, (char *)(mem->data) + mem->pos, amount);
    
    mem->pos = end;
    
    return amount;
}

static int MemoryHandle_Close(void *userdata) {
    MemoryHandleData *mem = (MemoryHandleData *)userdata;
    
    if (mem->freeOnClose) {
        DXFREE(mem->data);
    }
    
    DXFREE(mem);
    
    return 0;
}

static const PL_FileFunctions MemoryHandleFuncs = {
    MemoryHandle_GetSize,
    MemoryHandle_Tell,
    MemoryHandle_Seek,
    MemoryHandle_Read,
    MemoryHandle_Close
};

int PL_File_CreateHandleFromMemory(void *data, int length, int freeOnClose) {
    MemoryHandleData *mem = DXALLOC(sizeof(MemoryHandleData));
    mem->data = data;
    mem->length = length;
    mem->pos = 0;
    mem->freeOnClose = 0;
    
    return PL_File_CreateHandle(&MemoryHandleFuncs, (void *)mem);
}

/* --------------------------------------------------- Subsection handle */
typedef struct _SubsectionHandleData {
    int srcHandle;
    
    long long start;
    long long end;
    
    int closeOnClose;
} SubsectionHandleData;

static long long SubsectionHandle_GetSize(void *userdata) {
    SubsectionHandleData *sub = (SubsectionHandleData *)userdata;
    return sub->end - sub->start;
}
static long long SubsectionHandle_Tell(void *userdata) {
    SubsectionHandleData *sub = (SubsectionHandleData *)userdata;
    return PL_File_Tell(sub->srcHandle) - sub->start;
}
static int SubsectionHandle_Seek(void *userdata, long long position, int origin) {
    SubsectionHandleData *sub = (SubsectionHandleData *)userdata;
    switch(origin) {
        case 0: position += sub->start; break;
        case 1: position += PL_File_Tell(sub->srcHandle); break;
        case 2: position += sub->end; break;
    }
    if (position < sub->start) {
        position = sub->start;
    }
    if (position > sub->end) {
        position = sub->end;
    }
    PL_File_Seek(sub->srcHandle, position, 0);
    
    return 0;
}
static int SubsectionHandle_Read(void *userdata, void *data, int size) {
    SubsectionHandleData *sub = (SubsectionHandleData *)userdata;
    long long pos = PL_File_Tell(sub->srcHandle);
    long long max = sub->end - pos;
    int amount = size;
    if (amount > max) {
        amount = (int)max;
    }
    if (amount < 0) {
        return 0;
    }
    
    return PL_File_Read(sub->srcHandle, data, amount);
}

static int SubsectionHandle_Close(void *userdata) {
    SubsectionHandleData *sub = (SubsectionHandleData *)userdata;
    
    if (sub->closeOnClose) {
        PL_File_Close(sub->srcHandle);
    }
    
    DXFREE(sub);
    return 0;
}

static const PL_FileFunctions SubsectionHandleFuncs = {
    SubsectionHandle_GetSize,
    SubsectionHandle_Tell,
    SubsectionHandle_Seek,
    SubsectionHandle_Read,
    SubsectionHandle_Close
};

int PL_File_CreateHandleSubsection(int srcFileHandle, long long start, long long size, int closeOnClose) {
    SubsectionHandleData *sub = DXALLOC(sizeof(SubsectionHandleData));
    sub->srcHandle = srcFileHandle;
    sub->start = start;
    sub->end = start + size;
    sub->closeOnClose = closeOnClose;
    
    PL_File_Seek(srcFileHandle, start, 0);
    
    return PL_File_CreateHandle(&SubsectionHandleFuncs, (void *)sub);
}

/* ------------------------------------------------------ Main I/O funcs */
long long PL_File_GetSize(int fileHandle) {
    FileHandle *handle = (FileHandle *)PL_Handle_GetData(fileHandle, DXHANDLE_PLFILE);
    if (handle != NULL && handle->functions->getSize != NULL) {
        return handle->functions->getSize(handle->userdata);
    }
    return -1;
}

long long PL_File_Tell(int fileHandle) {
    FileHandle *handle = (FileHandle *)PL_Handle_GetData(fileHandle, DXHANDLE_PLFILE);
    if (handle != NULL && handle->functions->tell != NULL) {
        return handle->functions->tell(handle->userdata);
    }
    return -1;
}

long long PL_File_Seek(int fileHandle, long long position, int origin) {
    FileHandle *handle = (FileHandle *)PL_Handle_GetData(fileHandle, DXHANDLE_PLFILE);
    if (handle != NULL && handle->functions->seek != NULL) {
        return handle->functions->seek(handle->userdata, position, origin);
    }
    return -1;
}

long long PL_File_Read(int fileHandle, void *data, int size) {
    FileHandle *handle = (FileHandle *)PL_Handle_GetData(fileHandle, DXHANDLE_PLFILE);
    if (handle != NULL && handle->functions->read != NULL) {
        return handle->functions->read(handle->userdata, data, size);
    }
    return -1;
}

int PL_File_IsEOF(int fileHandle) {
    return (PL_File_Tell(fileHandle) == PL_File_GetSize(fileHandle)) ? DXTRUE : DXFALSE;
}

int PL_File_Close(int fileHandle) {
    FileHandle *handle = (FileHandle *)PL_Handle_GetData(fileHandle, DXHANDLE_PLFILE);
    if (handle != NULL) {
        if (handle->functions->close != NULL) {
            handle->functions->close(handle->userdata);
        }
        PL_Handle_ReleaseID(fileHandle, DXTRUE);
        return 0;
    }
    return -1;
}

/* ---------------------------------------------- Main/Handle management */
void PL_File_SetOpenReadFunction(PLFileOpenFileFunction func) {
    s_openReadFunction = func;
}

int PL_File_OpenRead(const DXCHAR *filename) {
    if (s_openReadFunction != NULL) {
        return s_openReadFunction(filename);
    } else {
        return PL_Platform_FileOpenReadDirect(filename);
    }
}

int PL_File_CreateHandle(const PL_FileFunctions *funcs, void *userdata) {
    FileHandle *handle;
    int fileDataID;
    
    if (userdata == NULL || funcs == NULL) {
        return -1;
    }
    
    fileDataID = PL_Handle_AcquireID(DXHANDLE_PLFILE);
    if (fileDataID < 0) {
        if (funcs->close != NULL) {
            funcs->close(userdata);
        }
        return -1;
    }
    
    handle = (FileHandle *)PL_Handle_AllocateData(fileDataID, sizeof(FileHandle));
    handle->functions = funcs;
    handle->userdata = userdata;
    
    return fileDataID;
}

int PL_File_Init() {
    /* nothing to do */
    return 0;
}

int PL_File_End() {
    int fileHandle;
    
    while ((fileHandle = PL_Handle_GetFirstIDOf(DXHANDLE_PLFILE)) >= 0) {
        PL_File_Close(fileHandle);
    }
    
    s_openReadFunction = NULL;
    
    return 0;
}