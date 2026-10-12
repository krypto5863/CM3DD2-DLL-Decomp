//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// KAG Parser Utility Class
//---------------------------------------------------------------------------

#ifndef KAGParserH
#define KAGParserH
#include <functional>
//---------------------------------------------------------------------------

//#include <windows.h>
//#include "tp_stub.h"
#include "tjs.h"
#include "tjsNative.h"
#include "ScriptMgnIntf.h"
#include "tjsArray.h"
#include "tjsDictionary.h"
#include "DebugIntf.h"
#include "StorageImpl.h"
#include "TextStream.h"
#include "MsgIntf.h"
#include "PluginImpl.h"
#include "CharacterSet.h"
#include "TransIntf.h"
#include "tjsHashSearch.h"
#include <vector>
using namespace TJS;
/*[*/
//---------------------------------------------------------------------------
// KAG Parser debug level
//---------------------------------------------------------------------------
enum tTVPKAGDebugLevel
{
	tkdlNone, // none is reported
	tkdlSimple, // simple report
	tkdlVerbose // complete report ( verbose )
};
/*]*/


//---------------------------------------------------------------------------
// tTVPCharHolder
//---------------------------------------------------------------------------
class tTVPCharHolder
{
	tjs_char *Buffer;
	size_t BufferSize;
public:
	tTVPCharHolder() : Buffer(NULL), BufferSize(0)
	{
	}
	~tTVPCharHolder()
	{
		Clear();
	}

	tTVPCharHolder(const tTVPCharHolder &ref) : Buffer(NULL), BufferSize(0)
	{
		operator =(ref);
	}

	void Clear()
	{
		if(Buffer) delete [] Buffer, Buffer = NULL;
		BufferSize = 0;
	}

	void operator =(const tTVPCharHolder &ref)
	{
		Clear();
		BufferSize = ref.BufferSize;
		Buffer = new tjs_char[BufferSize];
		memcpy(Buffer, ref.Buffer, BufferSize *sizeof(tjs_char));
	}

	void operator =(const tjs_char *ref)
	{
		Clear();
		if(ref)
		{
			BufferSize = TJS_strlen(ref) + 1;
			Buffer = new tjs_char[BufferSize];
			memcpy(Buffer, ref, BufferSize*sizeof(tjs_char));
		}
	}

	operator const tjs_char *() const
	{
		return Buffer;
	}

	operator tjs_char *()
	{
		return Buffer;
	}
};
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTVPScenarioCacheItem : Scenario Cache Item
//---------------------------------------------------------------------------
class tTVPScenarioCacheItem
{
public:
	struct tLine
	{
		const tjs_char *Start;
		tjs_int Length;
	};

private:
	tTVPCharHolder Buffer;
	tLine *Lines;
	tjs_int LineCount;

public:
	struct tLabelCacheData
	{
		tjs_int Line;
		tjs_int Count;
		tLabelCacheData(tjs_int line, tjs_int count)
		{
			Line = line;
			Count = count;
		}
	};

public:
	typedef tTJSHashTable<ttstr, tLabelCacheData> tLabelCacheHash;
private:
	tLabelCacheHash LabelCache; // Label cache
	std::vector<ttstr> LabelAliases;
	bool LabelCached; // whether the label is cached

	tjs_int RefCount;

public:
	tTVPScenarioCacheItem(const ttstr & name, bool istring);
protected:
	~tTVPScenarioCacheItem();
public:
	void AddRef();
	void Release();
private:
	void LoadScenario(const ttstr & name, bool isstring);
		// load file or string to buffer
public:
	const ttstr & GetLabelAliasFromLine(tjs_int line) const
		{ return LabelAliases[line]; }
	void EnsureLabelCache();

	tLine * GetLines() const { return Lines; }
	tjs_int GetLineCount() const { return LineCount; }
	const tLabelCacheHash & GetLabelCache() const { return LabelCache; }
};
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// tTJSNI_KAGParser
//---------------------------------------------------------------------------
class tTJSNI_KAGParser : public tTJSNativeInstance
{
	typedef tTJSNativeInstance inherited;

public:
	tTJSNI_KAGParser();
	tjs_error TJS_INTF_METHOD
		Construct(tjs_int numparams, tTJSVariant **param,
			iTJSDispatch2 *tjs_obj);
	void TJS_INTF_METHOD Invalidate();

private:
	iTJSDispatch2 * Owner; // owner object

	iTJSDispatch2 * DicClear; // Dictionary.Clear method pointer
	iTJSDispatch2 * DicAssign; // Dictionary
	iTJSDispatch2 * DicObj; // DictionaryObject

	iTJSDispatch2 * Macros; // Macro Dictionary Object

	std::vector<iTJSDispatch2 *> MacroArgs; // Macro arguments
	tjs_uint MacroArgStackDepth;
	tjs_uint MacroArgStackBase;

	struct tCallStackData
	{
		ttstr Storage; // caller storage
		ttstr Label; // caller nearest label
		tjs_int Offset; // line offset from the label
		ttstr OrgLineStr; // original line string
		ttstr LineBuffer; // line string (if alive)
		tjs_int Pos;
		bool LineBufferUsing; // whether LineBuffer is used or not
		tjs_uint MacroArgStackBase;
		tjs_uint MacroArgStackDepth;
		std::vector<tjs_int> ExcludeLevelStack;
		std::vector<bool> IfLevelExecutedStack;
        tjs_int ExcludeLevel;
        tjs_int IfLevel;

		tCallStackData(const ttstr &storage, const ttstr &label,
			tjs_int offset, const ttstr &orglinestr, const ttstr &linebuffer,
			tjs_int pos, bool linebufferusing, tjs_uint macroargstackbase,
			tjs_uint macroargstackdepth,
			const std::vector<tjs_int> &excludelevelstack, tjs_int excludelevel,
			const std::vector<bool> &iflevelexecutedstack, tjs_int iflevel) :
			Storage(storage), Label(label), Offset(offset), OrgLineStr(orglinestr),
			LineBuffer(linebuffer), Pos(pos), LineBufferUsing(linebufferusing),
			MacroArgStackBase(macroargstackbase),
			MacroArgStackDepth(macroargstackdepth),
			ExcludeLevelStack(excludelevelstack), ExcludeLevel(excludelevel),
			IfLevelExecutedStack(iflevelexecutedstack), IfLevel(iflevel) {;}
	};
	std::vector<tCallStackData> CallStack;

	tTVPScenarioCacheItem * Scenario;
	tTVPScenarioCacheItem::tLine * Lines; // is copied from Scenario
	tjs_int LineCount; // is copied from Scenario

	ttstr StorageName;
	ttstr StorageShortName;
	ttstr ScenarioStr; // cm3d2 local addition: text of a scenario loaded by LoadScenarioString (state key "scenarioStr")

	tjs_int CurLine; // current processing line
	tjs_int CurPos; // current processing position ( column )
	const tjs_char *CurLineStr; // current line string
	ttstr LineBuffer; // line buffer ( if any macro/emb was expanded )
	bool LineBufferUsing;
	ttstr CurLabel; // Current Label
	ttstr CurPage; // Current Page Name
	tjs_int TagLine; // line number of previous tag

	tTVPKAGDebugLevel DebugLevel; // debugging log level
	bool ProcessSpecialTags; // whether to process special tags
	bool IgnoreCR; // CR is not interpreted as [r] tag when this is true
	bool RecordingMacro; // recording a macro
	ttstr RecordingMacroStr; // recording macro content
	ttstr RecordingMacroName; // recording macro's name

	tTJSVariant ValueVariant;

	tjs_int ExcludeLevel;
	tjs_int IfLevel;

	std::vector<tjs_int> ExcludeLevelStack;
	std::vector<bool> IfLevelExecutedStack;

	bool Interrupted;

public:
	// ---- cm3d2 local additions (see RECONSTRUCTION-NOTES.md) ----
	const ttstr & GetStorageShortName() const { return StorageShortName; }
	iTJSDispatch2 * GetOwner() const { return Owner; }
	// whether the call / return tags are processed (DLL parser +0x131 / +0x132; semantics UNVERIFIED, default true)
	bool TagCallEnabled = true;
	bool TagReturnEnabled = true;
	// fired at the start of every LoadScenario (DLL parser std::function at +0x180, argument: storage name)
	std::function<void(const ttstr &)> OnLoadHook;
	// the DLL parser's other four callbacks (+0x1c0 label found, +0x200 jump, +0x240 call, +0x280 return)
	std::function<void(const ttstr &)> OnLabelHook;
	// fired when the parser passes a label line: (label, has page name, page name); DLL parser +0x140
	std::function<void(const ttstr &, bool, const ttstr &)> OnLabelPassHook;
	std::function<void(ttstr &, ttstr &)> OnJumpHook;
	std::function<void(ttstr &, ttstr &)> OnCallHook;
	std::function<void(const ttstr &, tjs_int)> OnReturnHook;
	void ClearCallStackExternal() { ClearCallStack(); }
	tjs_int GetCallStackDepthExternal() const { return (tjs_int)CallStack.size(); }
	void LoadScenarioString(const ttstr & text);
	static tTJSNI_KAGParser * FromObject(iTJSDispatch2 * object);
	// ---- end additions ----
	void operator = (const tTJSNI_KAGParser & ref);
	iTJSDispatch2 *Store();
	void Restore(iTJSDispatch2 *dic);

	void Clear(); // clear all states

private:
	void ClearBuffer(); // clear internal buffer

	void Rewind(); // set current position to first

	void BreakConditionAndMacro(); // break condition state and macro expansion

public:
	void LoadScenario(const ttstr & name);
	const ttstr & GetStorageName() const { return StorageName; }
	void GoToLabel(const ttstr &name); // search label and set current position
	void GoToStorageAndLabel(const ttstr &storage, const ttstr &label);
	void CallLabel(const ttstr &name);
private:
	bool SkipCommentOrLabel(); // skip comment or label and go to next line

	void PushMacroArgs(iTJSDispatch2 *args);
public:
	void PopMacroArgs();
private:
	void ClearMacroArgs();
	void PopMacroArgsTo(tjs_uint base);

	void FindNearestLabel(tjs_int start, tjs_int &labelline, ttstr &labelname);

	void PushCallStack();
	void PopCallStack(const ttstr &storage, const ttstr &label);
	void StoreIntStackToDic(iTJSDispatch2 *dic, std::vector<tjs_int> &stack, const tjs_char *membername);
	void StoreBoolStackToDic(iTJSDispatch2 *dic, std::vector<bool> &stack, const tjs_char *membername);
	void RestoreIntStackFromStr(std::vector<tjs_int> &stack, const ttstr &str);
	void RestoreBoolStackFromStr(std::vector<bool> &stack, const ttstr &str);

public:
	void ClearCallStack();

	void Interrupt() { Interrupted = true; };
	void ResetInterrupt() { Interrupted = false; };

private:
	iTJSDispatch2 * _GetNextTag();

public:
	iTJSDispatch2 * GetNextTag();

	const ttstr & GetCurLabel() const { return CurLabel; }
	tjs_int GetCurLine() const { return CurLine; }
	tjs_int GetCurPos() const { return CurPos; }
	const tjs_char* GetCurLineStr() const { return CurLineStr; }

	void SetProcessSpecialTags(bool b) { ProcessSpecialTags = b; }
	bool GetProcessSpecialTags() const { return ProcessSpecialTags; }

	void SetIgnoreCR(bool b) { IgnoreCR = b; }
	bool GetIgnoreCR() const { return IgnoreCR; }

	void SetDebugLevel(tTVPKAGDebugLevel level) { DebugLevel = level; }
	tTVPKAGDebugLevel GetDebugLevel(void) const { return DebugLevel; }

	iTJSDispatch2 *GetMacrosNoAddRef() const { return Macros; }

	iTJSDispatch2 *GetMacroTopNoAddRef() const;
		// get current macro argument (parameters)

	tjs_int GetCallStackDepth() const { return CallStack.size(); }

	void Assign(const tTJSNI_KAGParser &ref) { operator =(ref); }

};

extern iTJSDispatch2 * TVPCreateNativeClass_KAGParser();

#if 0
//---------------------------------------------------------------------------
// tTJSNC_KAGParser
//---------------------------------------------------------------------------
class tTJSNC_KAGParser : public tTJSNativeClass
{
public:
	tTJSNC_KAGParser();

	static tjs_uint32 ClassID;

private:
	iTJSNativeInstance *CreateNativeInstance();
};
//---------------------------------------------------------------------------
#endif

#endif
