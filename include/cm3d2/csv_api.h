#pragma once

#include "cm3d2/file_api.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* CM3D2_CsvHandle;

CM3D2_API CM3D2_CsvHandle CM3D2_CALL DLL_CSV_CreateCsvParser(void);
CM3D2_API void CM3D2_CALL DLL_CSV_DeleteCsvParser(CM3D2_CsvHandle csv);
CM3D2_API uint32_t CM3D2_CALL DLL_CSV_Open(
    CM3D2_CsvHandle csv,
    CM3D2_FileHandle file);

CM3D2_API void CM3D2_CALL DLL_CSV_GetCellAsBinary(
    CM3D2_CsvHandle csv,
    int32_t x,
    int32_t y,
    void* destination,
    int32_t capacity);
CM3D2_API void CM3D2_CALL DLL_CSV_GetCellAsString(
    CM3D2_CsvHandle csv,
    int32_t x,
    int32_t y,
    char* destination,
    int32_t capacity);
CM3D2_API uint32_t CM3D2_CALL DLL_CSV_GetCellBool(
    CM3D2_CsvHandle csv,
    int32_t x,
    int32_t y);
CM3D2_API int32_t CM3D2_CALL DLL_CSV_GetCellAsInteger(
    CM3D2_CsvHandle csv,
    int32_t x,
    int32_t y);
CM3D2_API float CM3D2_CALL DLL_CSV_GetCellAsReal(
    CM3D2_CsvHandle csv,
    int32_t x,
    int32_t y);

CM3D2_API int32_t CM3D2_CALL DLL_CSV_GetDataSizeString(
    CM3D2_CsvHandle csv,
    int32_t x,
    int32_t y);
CM3D2_API int32_t CM3D2_CALL DLL_CSV_GetDataSizeBinary(
    CM3D2_CsvHandle csv,
    int32_t x,
    int32_t y);
CM3D2_API uint32_t CM3D2_CALL DLL_CSV_IsCellToExistData(
    CM3D2_CsvHandle csv,
    int32_t x,
    int32_t y);
CM3D2_API uint32_t CM3D2_CALL DLL_CSV_IsValid(CM3D2_CsvHandle csv);
CM3D2_API int32_t CM3D2_CALL DLL_CSV_GetMaxCellX(CM3D2_CsvHandle csv);
CM3D2_API int32_t CM3D2_CALL DLL_CSV_GetMaxCellY(CM3D2_CsvHandle csv);

#ifdef __cplusplus
}
#endif
