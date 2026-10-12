// Native message defaults reconstructed from cm3d2.dll, SHA-256
// cacba71ac2ae6b8676f7628322d609cc704d185665e92a82fe74d4d34d3daba6.
// See rea-tjs-messages-evidence.json: each static initializer binds a name to
// a holder; holder+8 stores its literal default, holder+16 its assigned override.
// Data evidence: ev_83edf2d2914eea69880355e72577ac9c881ec6dacd28e205f522cc0694531244
// Literal evidence: ev_f4f9e526c28756da6cf46f413f9c9674158a8c9d78a782c57a2f39151b42d332
// Native parser default/override precedence:
// ev_610ae9641a707d50a3907b3cd88eeadc62b50e1381444dc98e8e5b1be5ff8a34
#include "tjs_messages.h"
#include "tjsCommHead.h"
#include "tjsError.h"
#include <mutex>

namespace cm3d2::internal {
void initializeTjsMessages() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        // TJSInternalError: holder 0x180214120, default 0x1801e0098; ev_f26b4c0824d0fbc5f6181a15deb9eb048ae5ac63c846bcef3821c5d0df7e924c
        TJS::TJSInternalError.AssignMessage(L"Internal error");
        // TJSWarning: holder 0x180214300, default 0x1801e00d0; ev_1b11856fca3fea59c848faa219518c09617608afe3478a1dc9bf22d38801bb5d
        TJS::TJSWarning.AssignMessage(L"Warning: ");
        // TJSWarnEvalOperator: holder 0x180213fd0, default 0x1801e0110; ev_3cc7231f639e2b2a9bc4d8723e7f783b7835e332144db8f8bc59f0c9dd2aab3b
        TJS::TJSWarnEvalOperator.AssignMessage(L"Non-global post-! operator is used (note that the post-! operator behavior is changed on TJS2 version 2.4.1)");
        // TJSNarrowToWideConversionError: holder 0x180214288, default 0x1801e0230; ev_cbd958aa3981149b8508e295efb8f3ab7ba42190439097a2e6539404195cccbb
        TJS::TJSNarrowToWideConversionError.AssignMessage(L"Cannot convert given narrow string to wide string");
        // TJSVariantConvertError: holder 0x180214030, default 0x1801e02d0; ev_898bc730064ab9206fad2821bf35f5e6020a7c1f2dd65a10ccf840e29aa43392
        TJS::TJSVariantConvertError.AssignMessage(L"Cannot convert the variable type (%1 to %2)");
        // TJSVariantConvertErrorToObject: holder 0x180213fa0, default 0x1801e0370; ev_0b9f76a2cd711ba480ed0178ddbf3b54fc8da27519f193eefffe8d5295f38e66
        TJS::TJSVariantConvertErrorToObject.AssignMessage(L"Cannot convert the variable type (%1 to Object)");
        // TJSIDExpected: holder 0x180214198, default 0x1801e03f0; ev_73619e051dcb49b54f731eedc6fc11c228fd0cc44c14dea0eb86b43e1ad82b06
        TJS::TJSIDExpected.AssignMessage(L"Specify an ID");
        // TJSSubstitutionInBooleanContext: holder 0x1802141f8, default 0x1801e0450; ev_41ff845e401db782fcf0e6d63e75781514b147e509431d82688e192343ecc9a2
        TJS::TJSSubstitutionInBooleanContext.AssignMessage(L"Substitution in boolean context (use form of '(A=B)!=0' to compare to zero)");
        // TJSCannotModifyLHS: holder 0x180214180, default 0x1801e0510; ev_894876843e355d56ab9659880aa1791eeaaa2dc7d12e8338a42f0cf52d04330f
        TJS::TJSCannotModifyLHS.AssignMessage(L"This expression cannot be used as a lvalue");
        // TJSInsufficientMem: holder 0x1802140c0, default 0x1801e0590; ev_ae767ca8a6fa134309b9645c267221c96e7d4bd635898672dd1017e05fc9c466
        TJS::TJSInsufficientMem.AssignMessage(L"Insufficient memory");
        // TJSCannotGetResult: holder 0x180214018, default 0x1801e05e0; ev_5779691bf1b0cbe90dca7506e6169452ebadc362bf2cb68758b820fe05d9b5ad
        TJS::TJSCannotGetResult.AssignMessage(L"Cannot get the value of this expression");
        // TJSNullAccess: holder 0x180214408, default 0x1801e0650; ev_e798cd2b94d0e8d8bf88cd1656ebeca266f70671cf14dea4aecef7a4b3960ae3
        TJS::TJSNullAccess.AssignMessage(L"Accessing to null object");
        // TJSMemberNotFound: holder 0x180214228, default 0x1801e06b0; ev_98062391be2e1e55e7e6173c3722effbee13ad8267f67ff50d5fe23edae730d5
        TJS::TJSMemberNotFound.AssignMessage(L"Member \"%1\" does not exist");
        // TJSMemberNotFoundNoNameGiven: holder 0x180214138, default 0x1801e0728; ev_249e766ac7e014d5cc8248769f085ac84d9c21de89f00243e601e85b29e0e915
        TJS::TJSMemberNotFoundNoNameGiven.AssignMessage(L"Member does not exist");
        // TJSNotImplemented: holder 0x180214168, default 0x1801e0780; ev_3455cb6a1ad9381d27acc8fedfe2d9e9109650f23241d356525901d3bbd10e36
        TJS::TJSNotImplemented.AssignMessage(L"Called method is not implemented");
        // TJSInvalidParam: holder 0x180213fb8, default 0x1801e07e8; ev_ef0ce0af4a70478a99a7269f82eea40ea0ee0caf516e2d7d5c44aec2e00d5252
        TJS::TJSInvalidParam.AssignMessage(L"Invalid argument");
        // TJSBadParamCount: holder 0x180213f70, default 0x1801e0838; ev_d48685b475bf29d179e6865cc11c57f7095747ded9494fbfa3bc83cec581c2e2
        TJS::TJSBadParamCount.AssignMessage(L"Invalid argument count");
        // TJSInvalidType: holder 0x180214210, default 0x1801e0890; ev_d25339aab9286fdcf1d9ee0279c889134bb92a9a36659ce2233ed3bf988f4a5d
        TJS::TJSInvalidType.AssignMessage(L"Not a function or invalid method/property type");
        // TJSSpecifyDicOrArray: holder 0x1802143d8, default 0x1801e0920; ev_fcd1ceab79145e0cd2e26522caba11f427a0e24afe2550d862080f9f851fdcd3
        TJS::TJSSpecifyDicOrArray.AssignMessage(L"Specify a Dictionary object or an Array object");
        // TJSSpecifyArray: holder 0x180214438, default 0x1801e09a0; ev_2a3fb4051aadf9f5590e6f0ea1404c85ee0223f616543bd454bc83cda3abb783
        TJS::TJSSpecifyArray.AssignMessage(L"Specify an Array object");
        // TJSStringDeallocError: holder 0x180214270, default 0x1801e0a00; ev_f8891bbb79fa5f1c6cd44f7dc165ad48a94bb9204c286437fe7eef41b9226b60
        TJS::TJSStringDeallocError.AssignMessage(L"Cannot free the string memory block");
        // TJSStringAllocError: holder 0x180213f10, default 0x1801e0a70; ev_7d17f86cde68b8b75d482e7bb72d1334ff72974183ac4c8e56312f8b566aada0
        TJS::TJSStringAllocError.AssignMessage(L"Cannot allocate the string memory block");
        // TJSMisplacedBreakContinue: holder 0x180213fe8, default 0x1801e0b00; ev_1ad16b6dce01a3461ffab992d82d6cfcf30ebf30057c4b9efc0e0e4c1ed0184e
        TJS::TJSMisplacedBreakContinue.AssignMessage(L"Cannot place \"break\" or \"continue\" here");
        // TJSMisplacedCase: holder 0x180214240, default 0x1801e0b78; ev_904e8b671ecf733fb8ffdb357921f70769131df6ee0221705105f861d600f84e
        TJS::TJSMisplacedCase.AssignMessage(L"Cannot place \"case\" here");
        // TJSMisplacedReturn: holder 0x180213f28, default 0x1801e0bd8; ev_7a53ed7e7e89b4305f3e416b4914a37605c17b4642486fd67975472e6683c01c
        TJS::TJSMisplacedReturn.AssignMessage(L"Cannot place \"return\" here");
        // TJSStringParseError: holder 0x180213f88, default 0x1801e0c40; ev_1092d3fe00c6f8dbafe50cf40df4e34fa77326ac86b234e8f65bc6376bfa006a
        TJS::TJSStringParseError.AssignMessage(L"Un-terminated string/regexp/octet literal");
        // TJSNumberError: holder 0x1802141e0, default 0x1801e0cb8; ev_7cc932d09bfa7f0059e47cc6cd6fea67d9e6518862f463f3fb8dc7272b232ba5
        TJS::TJSNumberError.AssignMessage(L"Cannot be parsed as a number");
        // TJSUnclosedComment: holder 0x1802140d8, default 0x1801e0d20; ev_95c2e0e78cdc869fe36b9ef59acfd50102c9608bac45d503ad4f1d4d003199a3
        TJS::TJSUnclosedComment.AssignMessage(L"Un-terminated comment");
        // TJSInvalidChar: holder 0x180214060, default 0x1801e0d70; ev_e039e500f681db604490104da72fc36b987eb32940d9053c928b9a63aa0e6b6f
        TJS::TJSInvalidChar.AssignMessage(L"Invalid character '%1'");
        // TJSExpected: holder 0x180214258, default 0x1801e0db8; ev_846061206103aeef16c08539bf7372e77b08eab068915a085aa585bad9f1fdb9
        TJS::TJSExpected.AssignMessage(L"Expected %1");
        // TJSSyntaxError: holder 0x180213f58, default 0x1801e0df0; ev_aa47da72925b0534593e71ea401d16fd8618b06b4e7b31e31d3e729c75030bcf
        TJS::TJSSyntaxError.AssignMessage(L"Syntax error (%1)");
        // TJSPPError: holder 0x1802143c0, default 0x1801e0e30; ev_a94a5e265c6ef801a8cca92c60fd8aa2dafdfcaeaee2dcfe1a471d6cd084f300
        TJS::TJSPPError.AssignMessage(L"Error in conditional compiling expression");
        // TJSCannotGetSuper: holder 0x1802141c8, default 0x1801e0eb0; ev_f0be336ecb7733d436e7ec0a9cc3ba471400949a437eec1eecea319556c9a2e1
        TJS::TJSCannotGetSuper.AssignMessage(L"Super class does not exist or cannot specify the super class");
        // TJSInvalidOpecode: holder 0x1802141b0, default 0x1801e0f58; ev_8b7d18c3e9f5ce370468944e322e1dd7aea848d17c9402733e3cf7f3769acf06
        TJS::TJSInvalidOpecode.AssignMessage(L"Invalid VM code");
        // TJSRangeError: holder 0x1802143f0, default 0x1801e0f98; ev_b32b024ee736a72e52c937a2d12b079789f13a22f35af55df6f8ee290f3d48c7
        TJS::TJSRangeError.AssignMessage(L"The value is out of the range");
        // TJSAccessDenyed: holder 0x180214078, default 0x1801e1000; ev_84dff15d659d95fe34e3d2faa83c759f9fcea8a8a1c876ddb710b3c6d0d9895c
        TJS::TJSAccessDenyed.AssignMessage(L"Invalid operation for Read-only or Write-only property");
        // TJSNativeClassCrash: holder 0x180214420, default 0x1801e1098; ev_6fa6fa6207fa56b9fabe07f89eeab25f44d6540e7a1157f6390a90917709d08d
        TJS::TJSNativeClassCrash.AssignMessage(L"Invalid object context");
        // TJSInvalidObject: holder 0x180213f40, default 0x1801e10f0; ev_74a1aeaa6114b201372072e7d0a9373bef151397c5cf5b4303e0bce337b6389d
        TJS::TJSInvalidObject.AssignMessage(L"The object is already invalidated");
        // TJSCannotOmit: holder 0x180214108, default 0x1801e11d0; ev_3e1524925a34e322929e8ad834a92382a301a186043e96a97cda1853dc6311d2
        TJS::TJSCannotOmit.AssignMessage(L"\"...\" is used out of functions");
        // TJSCannotParseDate: holder 0x180214390, default 0x1801e1238; ev_9c379fe7752c3b2213a41aaf13ea32f7b8bd4844a35949a3d599bb338d40bdab
        TJS::TJSCannotParseDate.AssignMessage(L"Invalid date format");
        // TJSInvalidValueForTimestamp: holder 0x1802143a8, default 0x1801e1298; ev_34a46db7538c65e617f72a30e5019b44a8b5bb50c3b6b46c11f0134136c246a9
        TJS::TJSInvalidValueForTimestamp.AssignMessage(L"Invalid value for date/time");
        // TJSExceptionNotFound: holder 0x180214000, default 0x1801e1300; ev_59fa550aec5209645891fc39465feb49db893bd7e2a5838b1f9b2f73b03ed40c
        TJS::TJSExceptionNotFound.AssignMessage(L"Cannot convert exception because \"Exception\" does not exist");
        // TJSInvalidFormatString: holder 0x1802140a8, default 0x1801e13a8; ev_214efcc592b9b9179d57395cb72e4669a78e29cb6e0bb871de12d4b1f19b6d4f
        TJS::TJSInvalidFormatString.AssignMessage(L"Invalid format string");
        // TJSDivideByZero: holder 0x180214048, default 0x1801e13f8; ev_25dc65785f2a8d218bef465fec37529abf2ee1582f80eb230fa1004e249f63c2
        TJS::TJSDivideByZero.AssignMessage(L"Division by zero");
        // TJSNotReconstructiveRandomizeData: holder 0x180214150, default 0x1801e1470; ev_75c4abdbfbfa9ec5c9cbeaac8a063028a76926eaaac98f0cf9d8f896f6b89507
        TJS::TJSNotReconstructiveRandomizeData.AssignMessage(L"Cannot reconstruct random seeds");
        // TJSSymbol: holder 0x1802140f0, default 0x1801e14c4; ev_1f72aa97556ca39d9cbeab58837f24a1e55d1899edba3284b50ae18cf7ae1bf6
        TJS::TJSSymbol.AssignMessage(L"ID");
        // TJSCallHistoryIsFromOutOfTJS2Script: holder 0x180214330, default 0x1801e1518; ev_c069702f467aa66bf1b1433b6b19dab58f7fc36a66d2b1900eb8d38833516369
        TJS::TJSCallHistoryIsFromOutOfTJS2Script.AssignMessage(L"[out of TJS2 script]");
        // TJSNObjectsWasNotFreed: holder 0x1802142e8, default 0x1801e1580; ev_4baa6b729d16d7d20c8f84f494efb9c11bc40cb466a7659500ac4c5c77af5a86
        TJS::TJSNObjectsWasNotFreed.AssignMessage(L"Total %1 Object(s) was not freed");
        // TJSObjectCreationHistoryDelimiter: holder 0x1802142b8, default 0x1801de3c0; ev_5ac5321b583d75f5c691bfe397c40123d5c46a06bc863a5fe5eb5744f557d310
        // Literal bytes outside the main message region: ev_f536759ce1a7419474f7832f3040d296dff97224e31131617d7163aba83d3a96
        TJS::TJSObjectCreationHistoryDelimiter.AssignMessage(L" <-- ");
        // TJSObjectWasNotFreed: holder 0x180214348, default 0x1801e15d0; ev_7dd10e1fb113382c0b3259ff93fa76237fde1c7a231269e425f27de9a25befb7
        TJS::TJSObjectWasNotFreed.AssignMessage(L"Object %1 [%2] wes not freed / The object was created at : %2");
        // TJSGroupByObjectTypeAndHistory: holder 0x1802142d0, default 0x1801e16a0; ev_b4a006cd1224bb8e1593b6e17ae74c5a601992a64cd5de2606f9fe7581337a9f
        TJS::TJSGroupByObjectTypeAndHistory.AssignMessage(L"Group by object type and location where the object was created");
        // TJSGroupByObjectType: holder 0x180214360, default 0x1801e1750; ev_3aad96413ffe6317f3abff38fad4de9fb7199cc310abfbffcd9e7d66d9f0d5f5
        TJS::TJSGroupByObjectType.AssignMessage(L"Group by object type");
        // TJSObjectCountingMessageGroupByObjectTypeAndHistory: holder 0x180214378, default 0x1801e17c0; ev_ea57886de4745278e3910aa5d14d5d68a869f68d7af4d65f5b14ecd26605c746
        TJS::TJSObjectCountingMessageGroupByObjectTypeAndHistory.AssignMessage(L"%1 time(s) : [%2] %3");
        // TJSObjectCountingMessageTJSGroupByObjectType: holder 0x180214318, default 0x1801e1820; ev_55943ee28724c9beeef82e9b70ef68c2080a69222f86778ff0882afd0efecc84
        TJS::TJSObjectCountingMessageTJSGroupByObjectType.AssignMessage(L"%1 time(s) : [%2]");
        // TJSWarnRunningCodeOnDeletingObject: holder 0x1802142a0, default 0x1801e18c0; ev_92e225036e9df253a17fb99bd5cfa9866f1cf1a61a0b1cb1642f6715254aac96
        TJS::TJSWarnRunningCodeOnDeletingObject.AssignMessage(L"%4: Running code on deleting-in-progress object %1[%2] / The object was created at : %3");
        // TJSWriteError: holder 0x180214450, default 0x1801e19d0; ev_c5cf5aa506c37787b6305c0cd891630079690b29187bee05cbf9cc1aa4ff671a
        TJS::TJSWriteError.AssignMessage(L"Write error");
        // TJSReadError: holder 0x180214468, default 0x1801e1a38; ev_f512ca2e974217eeea00c44d98f0830f8f7ef90a2dab2161080f63731a886c6c
        TJS::TJSReadError.AssignMessage(L"Read error");
        // TJSSeekError: holder 0x180214480, default 0x1801e1a70; ev_42a9cfde9dae6bc5f727fc3a20b5fa30aa5e1bb16d0628c0da2a72bd8234aa65
        TJS::TJSSeekError.AssignMessage(L"Seek error");
        // Eleven newer vendored TJS2 messages have no matching named holder in
        // this original DLL; leave those blank instead of inventing native text.
    });
}
} // namespace cm3d2::internal

