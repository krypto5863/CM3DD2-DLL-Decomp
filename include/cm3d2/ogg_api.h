#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  if defined(CM3D2_RECONSTRUCTION_BUILD)
#    define CM3D2_API __declspec(dllexport)
#  else
#    define CM3D2_API __declspec(dllimport)
#  endif
#  define CM3D2_CALL __cdecl
#else
#  define CM3D2_API
#  define CM3D2_CALL
#endif

/*
 * DLL_OGG_* (0x180072b60..0x180073040). A sound reader wraps an Ogg Vorbis stream.
 * The decoder is an embedded public-domain Vorbis decoder (stb_vorbis, MIT or
 * public domain) rather than the original's unnamed decoder. Output is 16-bit
 * interleaved PCM, which ReadUnity converts to float (value / 32767).
 */
typedef void* CM3D2_SoundReaderOggHandle;

CM3D2_API void CM3D2_CALL DLL_OGG_CreateSoundReaderOGG(void* file_system, CM3D2_SoundReaderOggHandle* out_reader);
CM3D2_API void CM3D2_CALL DLL_OGG_DeleteSoundReaderOGG(CM3D2_SoundReaderOggHandle* reader);
CM3D2_API void CM3D2_CALL DLL_OGG_OpenFile(CM3D2_SoundReaderOggHandle* reader, const char* utf8_path);
CM3D2_API uint32_t CM3D2_CALL DLL_OGG_IsOpenFile(CM3D2_SoundReaderOggHandle* reader);
CM3D2_API void CM3D2_CALL DLL_OGG_CloseFile(CM3D2_SoundReaderOggHandle* reader);
/* The managed declaration is uint DLL_OGG_Read(ref Data, IntPtr dest, uint read_size); the native export tail-calls the same
   block reader as ReadUnity (FUN_1800948e0). Returns the number of bytes written to dest. */
CM3D2_API uint32_t CM3D2_CALL DLL_OGG_Read(CM3D2_SoundReaderOggHandle* reader, void* destination, uint32_t read_size);
CM3D2_API void CM3D2_CALL DLL_OGG_ReadUnity(CM3D2_SoundReaderOggHandle* reader, float* destination, uint32_t sample_count);
CM3D2_API void CM3D2_CALL DLL_OGG_SeekSample(CM3D2_SoundReaderOggHandle* reader, int32_t sample, char absolute);
CM3D2_API uint32_t CM3D2_CALL DLL_OGG_GetChannels(CM3D2_SoundReaderOggHandle* reader);
CM3D2_API uint32_t CM3D2_CALL DLL_OGG_GetSampleRate(CM3D2_SoundReaderOggHandle* reader);
CM3D2_API uint32_t CM3D2_CALL DLL_OGG_GetSampleBit(CM3D2_SoundReaderOggHandle* reader);
CM3D2_API uint32_t CM3D2_CALL DLL_OGG_GetTotalSampleLengs(CM3D2_SoundReaderOggHandle* reader);

#ifdef __cplusplus
}
#endif
