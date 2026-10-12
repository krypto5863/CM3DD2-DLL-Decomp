#pragma once
#include "cm3d2/file_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * DLL_TJSVariant_* (0x180073340..0x180073670). A variant holds a tagged value:
 * 0 void, 1 object closure, 2 string, 3 octet, 4 integer, 5 real. Eval and host
 * callbacks can supply objects/octets; the setters here only create scalar values.
 * Created variants own their payload references until Clear, overwrite, or Delete.
 * Reference aliases a supplied variant without copying or retaining it. Callback
 * arguments/results are borrowed live views, valid only during that callback;
 * do not Delete or keep their handles. Set/Clear immediately mutate the VM slot.
 * Object-to-string conversion includes pointer text; octet-to-string and
 * object/octet-to-number conversions raise native-style conversion errors.
 * Release owned script objects/closures before deleting their engine: arbitrary
 * script-driven finalization after engine shutdown is not supported.
 */
typedef void* CM3D2_TjsVariantHandle;

CM3D2_API void CM3D2_CALL DLL_TJSVariant_Create(CM3D2_TjsVariantHandle* out_variant);
CM3D2_API void CM3D2_CALL DLL_TJSVariant_Reference(CM3D2_TjsVariantHandle* variant, void* value);
CM3D2_API void CM3D2_CALL DLL_TJSVariant_Delete(CM3D2_TjsVariantHandle* variant);
CM3D2_API void CM3D2_CALL DLL_TJSVariant_Clear(CM3D2_TjsVariantHandle* variant);
CM3D2_API int32_t CM3D2_CALL DLL_TJSVariant_GetType(CM3D2_TjsVariantHandle* variant);
CM3D2_API int64_t CM3D2_CALL DLL_TJSVariant_AsInteger(CM3D2_TjsVariantHandle* variant);
CM3D2_API float CM3D2_CALL DLL_TJSVariant_AsReal(CM3D2_TjsVariantHandle* variant);
CM3D2_API void CM3D2_CALL DLL_TJSVariant_AsString(
    CM3D2_TjsVariantHandle* variant,
    char* destination,
    int32_t capacity);
CM3D2_API void CM3D2_CALL DLL_TJSVariant_SetInteger(CM3D2_TjsVariantHandle* variant, int32_t value);
CM3D2_API void CM3D2_CALL DLL_TJSVariant_SetReal(CM3D2_TjsVariantHandle* variant, float value);
CM3D2_API void CM3D2_CALL DLL_TJSVariant_SetString(CM3D2_TjsVariantHandle* variant, const char* utf8);

#ifdef __cplusplus
}
#endif
