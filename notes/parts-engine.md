PartsEngine
=============

The PartsEngine library is Alicesoft's GUI library. This document tracks the changes in the
interface to this library across games.

The biggest change happened between Drapeko and Rance IX. Rance 01, which was released between
those two games, renamed the library to "GUIEngine" and also included substantial changes, but
it is omitted here.

Ideally, all versions of the library should be implemented on top of the same backend in
xsystem4.

## Oyako Rankan -> Pastel Chime 3

### Signature Changes

* AddCopyCutCGToPartsConstructionProcess
* AddDrawCutCGToPartsConstructionProcess
* SetHGaugeRate
* SetVGaugeRate
* AddMotionHGaugeRate
* AddMotionVGaugeRate

### New Functions

* AddDrawLineToPartsConstructionProcess
* BackPartsFlatBeginFrame
* ExistsFlatFile
* GetPartsPassCursor
* GoFramePartsFlat
* IsPartsFlatEnd
* SetPartsFlatAndStop
* SetPartsFlat
* StartPartsFlat
* StepPartsFlat
* StopPartsFlat
* GetPartsFlatCurrentFrameNumber
* GetPartsFlatEndFrame
* SetPassCursor

## Pastel Chime 3 -> Drapeko

Identical

## Drapeko -> Rance 9

### Signature Changes

* Many functions are now prefixed with "Parts_"
* Many functions have "Component" added to the name
* Only a few argument changes (noted below)

#### Name Changes

"Parts_" prefix added, otherwise unchanged:

* Parts\_AddAddFilterToPartsConstructionProcess
* Parts\_AddCopyCutCGToPartsConstructionProcess
* Parts\_AddCopyTextToPartsConstructionProcess
* Parts\_AddCreateCGToProcess
* Parts\_AddCreatePixelOnlyToPartsConstructionProcess
* Parts\_AddCreateToPartsConstructionProcess
* Parts\_AddDrawCutCGToPartsConstructionProcess
* Parts\_AddDrawLineToPartsConstructionProcess
* Parts\_AddDrawRectToPartsConstructionProcess
* Parts\_AddDrawTextToPartsConstructionProcess
* Parts\_AddFillAlphaColorToPartsConstructionProcess
* Parts\_AddFillAMapToPartsConstructionProcess
* Parts\_AddFillGradationHorizonToPartsConstructionProcess
* Parts\_AddFillToPartsConstructionProcess
* Parts\_AddFillWithAlphaToPartsConstructionProcess
* Parts\_AddGrayFilterToPartsConstructionProcess
* Parts\_AddMulFilterToPartsConstructionProcess
* Parts\_AddPartsText
* Parts\_BackPartsFlatBeginFrame
* Parts\_BuildPartsConstructionProcess
* Parts\_ClearPartsConstructionProcess
* Parts\_DeletePartsTopTextLine
* Parts\_ExistsFlatFile
* Parts\_GetPartsClickable
* Parts\_GetPartsPassCursor
* Parts\_GetPartsTextCountReturn
* Parts\_GetResetTimerByChangeInputStatus
* Parts\_GoFramePartsFlat
* Parts\_IsCursorIn
* Parts\_IsPartsFlatEnd
* Parts\_SetFont
* Parts\_SetHGaugeCG
* Parts\_SetHGaugeRate
* Parts\_SetHGaugeSurfaceArea
* Parts\_SetLoopCG
* Parts\_SetLoopCGSurfaceArea
* Parts\_SetNumeralCG
* Parts\_SetNumeralFont
* Parts\_SetNumeralLength
* Parts\_SetNumeralLinkedCGNumberWidthWidthList
* Parts\_SetNumeralNumber
* Parts\_SetNumeralShowComma
* Parts\_SetNumeralSpace
* Parts\_SetNumeralSurfaceArea
* Parts\_SetPartsCGDetectionSize
* Parts\_SetPartsCGDetectionSurfaceArea
* Parts\_SetPartsCG
* Parts\_SetPartsCGSurfaceArea
* Parts\_SetPartsConstructionSurfaceArea
* Parts\_SetPartsFlatAndStop
* Parts\_SetPartsFlat
* Parts\_SetPartsFontBoldWeight
* Parts\_SetPartsFontColor
* Parts\_SetPartsFontEdgeColor
* Parts\_SetPartsFontEdgeWeight
* Parts\_SetPartsFontSize
* Parts\_SetPartsFontType
* Parts\_SetPartsRectangleDetectionSize
* Parts\_SetPartsRectangleDetectionSurfaceArea
* Parts\_SetPartsTextSurfaceArea
* Parts\_SetTextCharSpace
* Parts\_SetText
* Parts\_SetTextLineSpace
* Parts\_SetThumbnailMode
* Parts\_SetThumbnailReductionSize
* Parts\_SetVGaugeCG
* Parts\_SetVGaugeRate
* Parts\_SetVGaugeSurfaceArea
* Parts\_StartPartsFlat
* Parts\_StepPartsFlatFinalFrame
* Parts\_StopPartsFlat
* Parts\_GetPartsUpperLeftPosX
* Parts\_GetPartsUpperLeftPosY
* Parts\_GetInputState
* Parts\_GetPartsFlatCurrentFrameNumber
* Parts\_GetPartsFlatEndFrame
* Parts\_GetPartsHeight
* Parts\_GetPartsWidth
* Parts\_GetSoundNumber
* Parts\_AddPartsTextHighlight
* Parts\_ClearPartsTextHighlight
* Parts\_GetPartsCGName
* Parts\_SetClickable
* Parts\_SetInputState
* Parts\_SetOnCursorShowLinkPartsNumber
* Parts\_SetParentPartsNumber
* Parts\_SetPartsPixelDecide
* Parts\_SetPartsTextCountReturn
* Parts\_SetPartsTextHighlight
* Parts\_SetPassCursor
* Parts\_SetResetTimerByChangeInputStatus
* Parts\_SetSoundNumber

Functions with "Component" added:

* AddMotionCG -> AddComponentMotionCG
* GetPartsMessageWindowShowLink -> IsComponentMessageWindowShowLink
* GetPartsShow -> IsComponentShow
* GetPartsX -> GetComponentPosX
* GetPartsY -> GetComponentPosY
* GetPartsZ -> GetComponentPosZ
* GetPartsAlpha -> GetComponentAlpha
* GetPartsOriginPosMode -> GetComponentOriginPosMode
* GetPartsSpeedupRateByMessageSkip -> GetComponentSpeedupRateByMessageSkip
* AddMotionAlpha -> AddComponentMotionAlpha
* AddMotionHGaugeRate -> AddComponentMotionHGaugeRate
* AddMotionMagX -> AddComponentMotionMagX
* AddMotionMagY -> AddComponentMotionMagY
* AddMotionNumeralNumber -> AddComponentMotionNumeralNumber
* AddMotionPos -> AddComponentMotionPos
* AddMotionRotateX -> AddComponentMotionRotateX
* AddMotionRotateY -> AddComponentMotionRotateY
* AddMotionRotateZ -> AddComponentMotionRotateZ
* AddMotionVGaugeRate -> AddComponentMotionVGaugeRate
* AddMotionVibrationSize -> AddComponentMotionVibrationSize
* SetAddColor -> SetComponentAddColor
* SetPartsAlphaClipperPartsNumber -> SetComponentAlphaClipper
* SetAlpha -> SetComponentAlpha
* SetPartsDrawFilter -> SetComponentDrawFilter
* SetPartsMagX -> SetComponentMagX
* SetPartsMagY -> SetComponentMagY
* SetPartsMessageWindowShowLink -> SetComponentMessageWindowShowLink
* SetMultiplyColor -> SetComponentMulColor
* SetPartsOriginPosMode -> SetComponentOriginPosMode
* SetPos -> SetComponentPos
  * argument types also change
* SetZ -> SetComponentPosZ
* SetPartsRotateX -> SetComponentRotateX
* SetPartsRotateY -> SetComponentRotateY
* SetPartsRotateZ -> SetComponentRotateZ
* SetShow -> SetComponentShow
* SetSpeedupRateByMessageSkip -> SetComponentSpeedupRateByMessageSkip
* Update -> UpdateComponent

Other:

* IsExistParts -> IsExist
* GetClickPartsNumber -> GetClickNumber
* GetFreeSystemPartsNumber -> GetFreeNumber
* ReleaseAllParts -> ReleaseAll
  * arguments also change
* ReleaseAllPartsWithoutSystem -> ReleaseAllWithoutSystem
  * arguments also change
* ReleaseParts -> Release
* SetFocusPartsNumber -> SetFocus

### New Functions

* AddComponentMotionCGTerination
* CrateActivityBinary
* GetMessageVariableBool
* IsButtonDrag
* IsButtonEnable
* IsButtonPixelDecide
* IsCheckBoxChecked
* IsCheckBoxDrag
* IsComponentMipmap
* IsFocus
* IsLayoutBoxReturn
* LoadParts
* Parts\_CreateParts3DLayerPluginID
* Parts\_GetPartsDrag
* Parts\_IsPartsPixelDecide
* Parts\_ReleaseParts3DLayerPluginID
* Parts\_SetPartsFlatSurfaceArea
* ReadActivityBinary
* ReleaseActivity
* SaveParts
* GetComponentMagX
* GetComponentMagY
* GetComponentRotateX
* GetComponentRotateY
* GetComponentRotateZ
* GetHScrollbarScrollRate
* GetMessageVariableFloat
* GetVScrollbarScrollRate
* AddController
* GetButtonB
* GetButtonClickSoundNumber
* GetButtonG
* GetButtonOnCursorSoundNumber
* GetButtonR
* GetCheckBoxB
* GetCheckBoxClickSoundNumber
* GetCheckBoxG
* GetCheckBoxOnCursorSoundNumber
* GetCheckBoxR
* GetComboBoxTextHeightMargin
* GetComboBoxTextWidthMargin
* GetComponentAddColorB
* GetComponentAddColorG
* GetComponentAddColorR
* GetComponentAlphaClipper
* GetComponentDrawFilter
* GetComponentHeight
* GetComponentMarginBottom
* GetComponentMarginLeft
* GetComponentMarginRight
* GetComponentMarginTop
* GetComponentMulColorB
* GetComponentMulColorG
* GetComponentMulColorR
* GetComponentTextureFilterType
* GetComponentType
* GetComponentWidth
* GetDelegateIndex
* GetHScrollbarClickSoundNumber
* GetHScrollbarLeftWidth
* GetHScrollbarMoveSizeByButton
* GetHScrollbarOnCursorSoundNumber
* GetHScrollbarRightWidth
* GetHScrollbarScrollPos
* GetHScrollbarTotalSize
* GetHScrollbarViewSize
* GetLayoutBoxAlign
* GetLayoutBoxLayoutType
* GetLayoutBoxReturnSize
* GetListBoxHeightMargin
* GetListBoxItemCount
* GetListBoxLineHeight
* GetListBoxOnCursorItemIndex
* GetListBoxScrollPos
* GetListBoxSelectIndex
* GetListBoxWidthMargin
* GetMessageDelegateIndex
* GetMessageDelegateIndex
* GetMessagePartsNumber
* GetMessageType
* GetMessageVariableCount
* GetMessageVariableInt
* GetMessageVariableType
* GetMultiTextBoxMaxTextLength
* GetMultiTextBoxSelectB
* GetMultiTextBoxSelectG
* GetMultiTextBoxSelectR
* GetTextBoxMaxTextLength
* GetTextBoxSelectB
* GetTextBoxSelectG
* GetTextBoxSelectR
* GetVScrollbarClickSoundNumber
* GetVScrollbarDownHeight
* GetVScrollbarMoveSizeByButton
* GetVScrollbarOnCursorSoundNumber
* GetVScrollbarScrollPos
* GetVScrollbarTotalSize
* GetVScrollbarUpHeight
* GetVScrollbarViewSize
* PartsFunc
* Parts\_GetOnCursorShowLinkPartsNumber
* Parts\_GetParentPartsNumber
* Parts\_GetParts3DLayerPluginID
* AddListBoxItem
* CheckBoxChecked
* ClearListBoxItem
* CloseTextBoxIME
* EraseListBoxItem
* GetButtonCGName
* GetButtonFontProperty
* GetButtonText
* GetCheckBoxCGName
* GetCheckBoxFontProperty
* GetCheckBoxText
* GetComboBoxCGName
* GetComboBoxFontProperty
* GetComboBoxText
* GetHScrollbarCGName
* GetListBoxCGName
* GetListBoxFontProperty
* GetListBoxItem
* GetListBoxOnCursorItem
* GetMessageVariableString
* GetMultiTextBoxCGName
* GetMultiTextBoxFontProperty
* GetMultiTextBoxText
* GetTextBoxCGName
* GetTextBoxFontProperty
* GetTextBoxText
* GetVScrollbarCGName
* InsertListBoxItem
* OpenTextBoxIME
* Parts\_ChangeFlatCG
* Parts\_ChangeFlatSound
* Parts\_GetPartsFlatDataInfo
* Parts\_SetDrag
* PopMessage
* ReleaseMessage
* RemoveController
* ResumeBuildView
* SetButtonCGName
* SetButtonClickSoundNumber
* SetButtonColor
* SetButtonDrag
* SetButtonEnable
* SetButtonFontProperty
* SetButtonOnCursorSoundNumber
* SetButtonPixelDecide
* SetButtonSize
* SetButtonText
* SetCheckBoxCGName
* SetCheckBoxClickSoundNumber
* SetCheckBoxColor
* SetCheckBoxDrag
* SetCheckBoxFontProperty
* SetCheckBoxOnCursorSoundNumber
* SetCheckBoxSize
* SetCheckBoxText
* SetComboBoxCGName
* SetComboBoxFontProperty
* SetComboBoxSize
* SetComboBoxText
* SetComboBoxTextMargin
* SetComponentMargin
* SetComponentMipmap
* SetComponentTextureFilterType
* SetComponentType
* SetDelegateIndex
* SetHScrollbarCGName
* SetHScrollbarClickSoundNumber
* SetHScrollbarLeftWidth
* SetHScrollbarMoveSizeByButton
* SetHScrollbarOnCursorSoundNumber
* SetHScrollbarRightWidth
* SetHScrollbarScrollPos
* SetHScrollbarScrollRate
* SetHScrollbarSize
* SetHScrollbarTotalSize
* SetHScrollbarViewSize
* SetLayoutBoxAlign
* SetLayoutBoxLayoutType
* SetLayoutBoxReturn
* SetListBoxCGName
* SetListBoxFontProperty
* SetListBoxItem
* SetListBoxLineHeight
* SetListBoxMargin
* SetListBoxScrollPos
* SetListBoxSelectIndex
* SetListBoxSize
* SetMultiTextBoxCGName
* SetMultiTextBoxFontProperty
* SetMultiTextBoxMaxTextLength
* SetMultiTextBoxSelectColor
* SetMultiTextBoxSize
* SetMultiTextBoxText
* SetTextBoxCGName
* SetTextBoxFontProperty
* SetTextBoxMaxTextLength
* SetTextBoxSelectColor
* SetTextBoxSize
* SetTextBoxText
* SetVScrollbarCGName
* SetVScrollbarClickSoundNumber
* SetVScrollbarDownHeight
* SetVScrollbarMoveSizeByButton
* SetVScrollbarOnCursorSoundNumber
* SetVScrollbarScrollPos
* SetVScrollbarScrollRate
* SetVScrollbarSize
* SetVScrollbarTotalSize
* SetVScrollbarUpHeight
* SetVScrollbarViewSize
* StopSoundWithoutSystemSound
* SuspendBuildViewAt
* SuspendBuildView

### Removed Functions

BackPartsFlashBeginFrame
ExistsFlashFile
GoFramePartsFlash
Init
IsPartsFlashEnd
SetPartsFlashAndStop
SetPartsFlash
SetPartsFlashSurfaceArea
SetPartsGroupNumber
StartPartsFlash
StepPartsFlashFinalFrame
StopPartsFlash
GetFocusPartsNumber
GetFreeSystemPartsNumberNotSaved
GetPartsFlashCurrentFrameNumber
GetPartsFlashEndFrame
GetAddColor
GetMultiplyColor
PopGUIController
PushGUIController
SetPartsGroupDecideClick
SetPartsGroupDecideOnCursor
UpdateGUIController

## Rance IX -> Blade Briders

Identical

## Blade Briders -> Evenicle

### Signature Changes

* AddMotionSound
* Parts\_ChangeFlatSound
* SetLayoutBoxReturn

### New Functions

* IsLayoutBoxReturnSizeForRate
* IsMultiTextBoxReadOnly
* IsOpenTextBoxIME
* LoadBackScene
* Parts\_IsPosForRate
* Parts\_IsTextEnableTag
* SaveBackScene
* GetLayoutBoxReturnSize
* GetActiveParts
* GetMultiTextBoxBGB
* GetMultiTextBoxBGG
* GetMultiTextBoxBGR
* GetMultiTextBoxFrameB
* GetMultiTextBoxFrameG
* GetMultiTextBoxFrameR
* GetMultiTextBoxReadOnlyBGB
* GetMultiTextBoxReadOnlyBGG
* GetMultiTextBoxReadOnlyBGR
* GetTextBoxBGB
* GetTextBoxBGG
* GetTextBoxBGR
* GetTextBoxFrameB
* GetTextBoxFrameG
* GetTextBoxFrameR
* GetTextBoxReadOnlyB
* GetTextBoxReadOnlyG
* GetTextBoxReadOnlyR
* Parts\_GetRubyCharSpace
* Parts\_GetTextPos
* Parts\_GetTextShowTime
* AddComponentMotionAddColor
* AddComponentMotionMulColor
* ClearBackScene
* GetButtonFlatName
* GetCheckBoxFlatName
* GetClickMissSoundName
* GetHScrollbarFlatName
* GetVScrollbarFlatName
* Parts\_GetRubyFont
* Parts\_GetSoundName
* Parts\_JumpFlatLabel
* Parts\_PlaySound
* Parts\_SetPosForRate
* Parts\_SetRubyCharSpace
* Parts\_SetRubyFont
* Parts\_SetSoundName
* Parts\_SetTextEnableTag
* Parts\_SetTextPos
* Parts\_SetTextShowTime
* SetAlphaForBackScene
* SetButtonFlatName
* SetCheckBoxFlatName
* SetClickMissSoundName
* SetFontColorForBackscene
* SetHScrollbarFlatName
* SetLayoutBoxReturnSizeForRate
* SetMulColorForBackScene
* SetMultiTextBoxBGColor
* SetMultiTextBoxFrameColor
* SetMultiTextBoxReadOnlyBGColor
* SetMultiTextBoxReadOnly
* SetOpenTextBoxIME
* SetShowForBackScene
* SetTextBoxBGColor
* SetTextBoxFrameColor
* SetTextBoxReadOnlyBGColor
* SetVScrollbarFlatName

### Removed Functions

* IsButtonDrag
* IsButtonPixelDecide
* IsCheckBoxDrag
* Parts\_SetThumbnailMode
* Parts\_SetThumbnailReductionSize
* SaveWithoutHideParts
* GetButtonClickSoundNumber
* GetButtonOnCursorSoundNumber
* GetCheckBoxClickSoundNumber
* GetCheckBoxOnCursorSoundNumber
* GetClickMissSoundNumber
* GetHScrollbarClickSoundNumber
* GetHScrollbarOnCursorSoundNumber
* GetLayoutBoxReturnSize
* GetVScrollBarClickSoundNumber
* GetVScrollbarOnCursorSoundNumber
* Parts\_GetSoundNumber
* CloseTextBoxIME
* OpenTextBoxIME
* Parts\_SetSoundNumber
* ReleaseAll
* SetButtonClickSoundNumber
* SetButtonDrag
* SetButtonOnCursorSoundNumber
* SetButtonPixelDecide
* SetCheckBoxClickSoundNumber
* SetCheckBoxDrag
* SetCheckBoxOnCursorSoundNumber
* SetClickMissSoundNumber
* SetHScrollbarClickSoundNumber
* SetHScrollbarOnCursorSoundNumber
* SetVScrollbarClickSoundNumber
* SetVScrollbarOnCursorSoundNumber

## Evenicle -> Rance 03

### Signature Changes

None.

### New Functions

* AddActivityParts
* CreateActivity
* CreatePartsMovie
* GetActivityParts
* IsCheckBoxButtonMode
* IsEndPartsMovie
* IsExistActivityCloseParts
* IsExistActivityEndKey
* IsExistActivityFile
* IsExistActivityIntentData
* IsExistActivityPartsByName
* IsExistActivityPartsByNumber
* IsExistActivity
* IsExistChild
* IsLockInputState
* IsNumeralShowPadding
* IsNumerarlFullPitch (sic)
* IsRadioButtonBoxExistGUI
* IsTextBoxReadOnly
* IsWantSaveBackScene
* LoadActivityEXText
* Parts\_IsNumeralShow
* Parts\_IsPartsFlatStop
* PlayPartsMovie
* ReadActivityFile
* ReleasePartsMovie
* RemoveActivityParts
* RemoveAllActivityParts
* SaveActivityEXText
* SaveThumbnail
* WriteActivityFile
* GetComponentAbsolutePosX
* GetComponentAbsolutePosY
* GetFlatFPSRate
* Parts\_GetHGaugeDenominator
* Parts\_GetHGaugeNumerator
* Parts\_GetVGaugeDenominator
* Parts\_GetVGaugeNumerator
* GetActiveController
* GetActivityEndKey
* GetActivityIntentDataType
* GetActivityPartsNumber
* GetButtonCharSpace
* GetButtonLineSpace
* GetButtonTextOriginPosMode
* GetCheckBoxButtonHeight
* GetCheckBoxButtonWidth
* GetCheckBoxCharSpace
* GetCheckBoxLineSpace
* GetCheckBoxTextOriginPosMode
* GetChildIndex
* GetChild
* GetComboBoxCharSpace
* GetComponentAbsoluteMaxPosZ
* GetComponentAbsolutePosZ
* GetComponentCheckBoxShowLinkNumber
* GetComponentScrollAlphaLinkNumber
* GetComponentScrollPosXLinkNumber
* GetComponentScrollPosYLinkNumber
* GetControllerID
* GetControllerIndex
* GetControllerLength
* GetFlatFPS
* GetLayoutBoxPaddingBottom
* GetLayoutBoxPaddingLeft
* GetLayoutBoxPaddingRight
* GetLayoutBoxPaddingTop
* GetListBoxCharSpace
* GetLoopCGNumofCG
* GetLoopCGStartNo
* GetMultiTextBoxCharSpace
* GetMultiTextBoxLineSpace
* GetNumeralLength
* GetRadioButtonBoxChild
* GetSystemOverlayController
* GetTextBoxCharSpace
* NumofActivityEndKey
* NumofActivityIntentDataDestination
* NumofActivityParts
* NumofChild
* NumofFlatCG
* NumofFlatSound
* NumofRadioButtonBoxChild
* Parts\_GetLoopCGTimeParCG
* Parts\_GetNumeralCGType
* Parts\_GetNumeralNumber
* Parts\_GetNumeralSpace
* Parts\_GetPartsCGDeform
* Parts\_GetPartsConstructionProcessCount
* Parts\_GetTextCharSpace
* Parts\_GetTextLineSpace
* AddActivityCloseParts
* AddActivityIntentDataDestination
* AddChild
* AddPartsConstructionProcess
* AddRadioButtonBoxChild
* ClearChangeFlatCG
* ClearChangeFlatSound
* ClearChild
* ClearRadioButtonBoxChild
* EraseActivityEndKey
* GetActivityIntentDataDestination
* GetActivityPartsName
* GetCGDetectionSurfaceArea
* GetComponentAbsolutePos
* GetConstructionSurfaceArea
* GetFlatCGName
* GetFlatSoundName
* GetFlatSurfaceArea
* GetHGaugeSurfaceArea
* GetLoopCGCGName
* GetLoopCGSurfaceArea
* GetNumeralCGNumberWidthList
* GetNumeralFont
* GetNumeralSurfaceArea
* GetPartsCGSurfaceArea
* GetPartsConstructionProcess
* GetPartsTextFontProperty
* GetPartsTextSurfaceArea
* GetProxyFlatCGName
* GetProxyFlatSound
* GetRectangleDetectionPoint
* GetRectangleDetectionSurfaceArea
* GetTextPartsText
* GetVGaugeSurfaceArea
* InsertChild
* Parts\_GetHGaugeCG
* Parts\_GetNumeralCGName
* Parts\_GetPartsCGDetectionCGName
* Parts\_GetPartsFlatName
* Parts\_GetVGaugeCG
* PauseMotion
* RemoveActivityCloseParts
* RemoveAllActivityCloseParts
* RemoveChild
* RemoveRadioButtonBoxChild
* ReturnChangeFlatCG
* ReturnChangeFlatSound
* SelectTextBoxAll
* SetActiveController
* SetActivityEndKey
* SetActivityIntentData
* SetButtonCharSpace
* SetButtonLineSpace
* SetButtonTextOriginPosMode
* SetCheckBoxButtonMode
* SetCheckBoxCharSpace
* SetCheckBoxLineSpace
* SetCheckBoxTextOriginPosMode
* SetComboBoxCharSpace
* SetComponentCheckBoxShowLinkNumber
* SetComponentScrollAlphaLinkNumber
* SetComponentScrollPosXLinkNumber
* SetComponentScrollPosYLinkNumber
* SetFlatFPSRate
* SetLayoutBoxPadding
* SetListBoxCharSpace
* SetLockInputState
* SetMultiTextBoxCharSpace
* SetMultiTextBoxLineSpace
* SetNumeralFont
* SetNumeralFullPitch
* SetNumeralShowPadding
* SetNumeralShowType
* SetRectangleDetectionPoint
* SetTextBoxCharSpace
* SetTextBoxReadOnly
* SetWantSaveBackScene

### Removed Functions

* CrateActivityBinary
* Parts\_SetNumeralFont
* ReadActivityBinary
* PartsFunc

## Rance 03 -> Tsumamigui 3

### Signature Changes

None.

### New Functions

* GetActivityEXID
* GetMultiTextBoxPaddingBottom
* GetMultiTextBoxPaddingLeft
* GetMultiTextBoxPaddingRight
* GetMultiTextBoxPaddingTop
* Parts\_GetSwipeType
* GetActivityEXText
* Parts\_SetSwipeType
* Parts\_StopSwipe
* SetActivityEXText
* SetMultiTextBoxPadding
* UpdateMatrix

### Removed Functions

* Parts\_AddAddFilterToPartsConstructionProcess
* Parts\_AddCopyCutCGToPartsConstructionProcess
* Parts\_AddCopyTextToPartsConstructionProcess
* Parts\_AddCreateCGToProcess
* Parts\_AddCreatePixelOnlyToPartsConstructionProcess
* Parts\_AddCreateToPartsConstructionProcess
* Parts\_AddDrawCutCGToPartsConstructionProcess
* Parts\_AddDrawLineToPartsConstructionProcess
* Parts\_AddDrawRectToPartsConstructionProcess
* Parts\_AddDrawTextToPartsConstructionProcess
* Parts\_AddFillAlphaColorToPartsConstructionProcess
* Parts\_AddFillAMapToPartsConstructionProcess
* Parts\_AddFillGradationHorizonToPartsConstructionProcess
* Parts\_AddFillToPartsConstructionProcess
* Parts\_AddFillWithAlphaToPartsConstructionProcess
* Parts\_AddGrayFilterToPartsConstructionProcess
* Parts\_AddMulFilterToPartsConstructionProcess

## Tsumamigui 3 -> Heartful Maman

### Signature Changes

* GetComponentAbsolutePos -> GetComponentAbsoluteSquarePos
  * Simplified version of GetComponentAbsolutePos still exists
* UpdateComponent
  * More arguments
* UpdateMatrix
  * More arguments

### New Functions

* BackMessageWindowFlatBeginFrame
* GetComponentReverseLR
* GetComponentReverseTB
* IsCheckBoxEnable
* IsComponentMessageWindowEffectLink
* IsFixedMessageWindowText
* IsKeyWaitShow
* IsMultiTextBoxHScrollShow
* IsMultiTextBoxVScrollShow
* IsOverMessageWindowFlatShowWaitFrame
* IsSpinBoxReadOnly
* Parts\_IsAnchorBottom
* Parts\_IsAnchorLeft
* Parts\_IsAnchorRight
* Parts\_IsAnchorTop
* Parts\_IsFixedText
* Parts\_IsLockUpdate
* StepMessageWindowFlatFinalFrame
* GetSpinBoxDefaultValue
* GetSpinBoxIncrementalValue
* GetSpinBoxMaxValue
* GetSpinBoxMinValue
* GetSpinBoxValue
* GetFormCaptionB
* GetFormCaptionG
* GetFormCaptionR
* GetHScrollTotalSizeLinkNumber
* GetHScrollViewSizeLinkNumber
* GetKeyWaitPosX
* GetKeyWaitPosY
* GetKeyWaitPosZ
* GetMessageWindowFlatShowWaitFrameNumber
* GetMessageWindowInactiveMultipleColorB
* GetMessageWindowInactiveMultipleColorG
* GetMessageWindowInactiveMultipleColorR
* GetMessageWindowTextOriginPosMode
* GetMessageWindowTextSpeed
* GetMotionBeginFrame
* GetMotionEndFrame
* GetPanelA
* GetPanelAlphaGradationBottom
* GetPanelAlphaGradationLeft
* GetPanelAlphaGradationRight
* GetPanelAlphaGradationTop
* GetPanelB
* GetPanelG
* GetPanelR
* GetSpinBoxBGB
* GetSpinBoxBGG
* GetSpinBoxBGR
* GetSpinBoxCharSpace
* GetSpinBoxDecimalPlace
* GetSpinBoxFrameB
* GetSpinBoxFrameG
* GetSpinBoxFrameR
* GetSpinBoxReadOnlyBGB
* GetSpinBoxReadOnlyBGG
* GetSpinBoxReadOnlyBGR
* GetSpinBoxSelectB
* GetSpinBoxSelectG
* GetSpinBoxSelectR
* GetVScrollTotalSizeLinkNumber
* GetVScrollViewSizeLinkNumber
* GetActivityEXText
* GetActivityIntentDataDestination
* GetActivityPartsName
* GetButtonCGName
* GetButtonFlatName
* GetButtonText
* GetCheckBoxCGName
* GetCheckBoxFlatName
* GetCheckBoxText
* GetClickMissSoundName
* GetComboBoxCGName
* GetComboBoxText
* GetFlatCGName
* GetFlatSoundName
* GetFormIcon
* GetFormTitle
* GetHScrollbarCGName
* GetHScrollbarFlatName
* GetKeyWaitFlatName
* GetListBoxCGName
* GetListBoxItem
* GetListBoxOnCursorItem
* GetLoopCGCGName
* GetMessageVariableString
* GetMessageWindowCGName
* GetMessageWindowFlatName
* GetMessageWindowText
* GetMultiTextBoxCGName
* GetMultiTextBoxText
* GetProxyFlatCGName
* GetProxyFlatSound
* GetSpinBoxCGName
* GetTextBoxCGName
* GetTextBoxText
* GetTextPartsText
* GetVScrollbarCGName
* GetVScrollbarFlatName
* Parts\_GetHGaugeCG
* Parts\_GetNumeralCGName
* Parts\_GetPartsCGDetectionCGName
* Parts\_GetPartsCGName
* Parts\_GetPartsFlatDataInfo
* Parts\_GetPartsFlatName
* Parts\_GetSoundName
* Parts\_GetVGaugeCG
* AddListBoxItem
* FixMessageWindowText
* FormClose
* GetKeyWaitCGName
* GetMessageWindowTextArea
* GetMessageWindowTextFont
* GetMessageWindowTextSpace
* GetSpinBoxFontProperty
* MoveController
* Parts\_FixText
* Parts\GetPartsSize
* Parts\_GetPartsUpperLeftPos
* Parts\_JumpFlatLabel
* Parts\_SetAnchor
* Parts\_SetLockUpdate
* Parts\_SetUpdateLookAtPos
* SelectSpinBoxAll
* SetCheckBoxEnable
* SetComponentMessageWindowEffectLink
* SetComponentReverseLR
* SetComponentReverseTB
* SetComponentVibrationPos
* SetFormCaptionColor
* SetFormIcon
* SetFormSize
* SetFormTitle
* SetHScrollbarCGName
* SetHScrollbarFlatName
* SetHScrollTotalSizeLinkNumber
* SetHScrollViewSizeLinkNumber
* SetKeyWaitCGName
* SetKeyWaitFlatName
* SetKeyWaitPos
* SetKeyWaitShow
* SetMessageWindowActive
* SetMessageWindowCGName
* SetMessageWindowFlatName
* SetMessageWindowFlatShowWaitFrameNumber
* SetMessageWindowInactiveMultipleColor
* SetMessageWindowTextArea
* SetMessageWindowTextFont
* SetMessageWindowText
* SetMessageWindowTextOriginPosMode
* SetMessageWindowTextSpace
* SetMessageWindowTextSpeed
* SetMotionData
* SetMultiTextBoxHScrollShow
* SetMultiTextBoxVScrollShow
* SetPanelAlphaGradationBottom
* SetPanelAlphaGradationLeft
* SetPanelAlphaGradationRight
* SetPanelAlphaGradationTop
* SetPanelColor
* SetPanelSize
* SetSpinBoxBGColor
* SetSpinBoxCGName
* SetSpinBoxCharSpace
* SetSpinBoxDecimalPlace
* SetSpinBoxDefaultValue
* SetSpinBoxFontProperty
* SetSpinBoxFrameColor
* SetSpinBoxIncrementalValue
* SetSpinBoxMaxValue
* SetSpinBoxMinValue
* SetSpinBoxReadOnlyBGColor
* SetSpinBoxReadOnly
* SetSpinBoxSelectColor
* SetSpinBoxSize
* SetSpinBoxValue
void SetVScrollTotalSizeLinkNumber(int Number, int LinkPartsNumber);
void SetVScrollViewSizeLinkNumber(int Number, int LinkPartsNumber);


### Removed Functions

* AddComponentMotionCG
* AddComponentMotionCGTermination
* GetComponentHeight
* GetComponentWidth
* AddComponentMotionAddColor
* AddComponentMotionAlpha
* AddComponentMotionHGaugeRate
* AddComponentMotionMagX
* AddComponentMotionMagY
* AddComponentMotionMulColor
* AddComponentMotionNumeralNumber
* AddComponentMotionPos
* AddComponentMotionRotateX
* AddComponentMotionRotateY
* AddComponentMotionRotateZ
* AddComponentMotionVGaugeRate
* AddComponentMotionVibrationSize
* AddListBoxItem
* AddMotionSound
* AddWholeMotionVibrationSize
* BeginMotion
* EndMotion
* GetActivityEXText
* GetActivityIntentDataDestination
* GetActivityPartsName
* GetButtonCGName
* GetButtonFlatName
* GetButtonText
* GetCheckBoxCGName
* GetCheckBoxFlatName
* GetCheckBoxText
* GetClickMissSoundName
* GetComboBoxCGName
* GetComboBoxText
* GetFlatCGName
* GetFlatSoundName
* GetHScrollbarCGName
* GetHScrollbarFlatName
* GetListBoxCGName
* GetListBoxItem
* GetListBoxOnCursorItem
* GetLoopCGCGName
* GetMessageVariableString
* GetMultiTextBoxCGName
* GetMultiTextBoxText
* GetProxyFlatCGName
* GetProxyFlatSound
* GetTextBoxCGName
* GetTextBoxText
* GetTextPartsText
* GetVScrollbarCGName
* GetVScrollbarFlatName
* Parts\_GetHGaugeCG
* Parts\_GetNumeralCGName
* Parts\_GetPartsCGDetectionCGName
* Parts\_GetPartsCGName
* Parts\_GetPartsFlatDataInfo
* Parts\_GetPartsFlatName
* Parts\_GetSoundName
* Parts\_GetVGaugeCG
* Parts\_JumpFlatLabel
* PauseMotion
* ResumeBuildView
* SeekEndMotion
* SetAlphaForBackScene
* SetHScrollbarCGName
* SetHScrollbarFlatName
* SetMulColorForBackScene
* SuspendBuildViewAt
* SuspendBuildView
* UpdateMotionTime

## Heartful Maman -> Ixseal

### Signature Changes

* LoadActivityEXText
* ReadActivityFile

These functions use the new ain v11 array type, but are otherwise unchanged:

* LoadBackScene
* LoadParts
* Load
* SaveBackScene
* SaveParts
* Save
* AddPartsConstructionProcess
* GetPartsConstructionProcess
* ReleaseAllWithoutSystem
* RemoveController

### New Functions

* IsComponentEnableClipArea
* IsComponentShowEditor
* IsExistActivityLockedParts
* IsExistWorkerThread
* IsFormEnableCloseButton
* IsFormFold
* IsFormResizable
* Parts\_BuildPartsConstructionProcessThread
* Parts\_IsFlatEnableLayer
* Parts\_IsThreadLoading
* Parts\_SetPartsCGThread
* Parts\_SetPartsFlatThread
* GetComboBoxListBoxCharSpace
* GetComboBoxListBoxHeight
* GetComboBoxListBoxHeightMargin
* GetComboBoxListBoxItemCount
* GetComboBoxListBoxLineHeight
* GetComboBoxListBoxScrollPos
* GetComboBoxListBoxSelectIndex
* GetComboBoxListBoxWidthMargin
* GetComponentClipAreaPosHeight
* GetComponentClipAreaPosWidth
* GetComponentClipAreaPosX
* GetComponentClipAreaPosY
* GetComponentTextureAddressType
* GetFlatGameViewHeight
* GetFlatGameViewWidth
* GetFormCaptionPlusHeight
* GetFormClientB
* GetFormClientG
* GetFormClientR
* GetFormGroupFormNumber
* GetFormHScrollPos
* GetFormHScrollTotalSize
* GetFormHScrollViewSize
* GetFormMaxHeight
* GetFormMaxWidth
* GetFormMinHeight
* GetFormMinWidth
* GetFormVScrollPos
* GetFormVScrollTotalSize
* GetFormVScrollViewSize
* NumofFormGroupForm
* NumofFormGroupTable
* NumofFormGroupVList
* Parts\_GetFlatFrame
* GetActivityBG
* GetComboBoxListBoxItem
* GetUserComponentData
* GetUserComponentName
* Parts\_GetComment
* AddActivityLockedParts
* AddComboBoxListBoxItem
* AddFormGroupFormNumber
* AddFormGroupTable
* AddFormGroupVList
* ClearComboBoxListBoxItem
* Debug\_GetSpriteRect
* EraseComboBoxListBoxItem
* GetComboBoxListBoxFontProperty
* GetFormGroupViewSnap
* InsertComboBoxListBoxItem
* Parts\_SetComment
* Parts\_SetFlatEnableLayer
* Parts\_SetWheelable
* SetActivityBG
* SetComboBoxListBoxCharSpace
* SetComboBoxListBoxFontProperty
* SetComboBoxListBoxHeight
* SetComboBoxListBoxItem
* SetComboBoxListBoxLineHeight
* SetComboBoxListBoxMargin
* SetComboBoxListBoxScrollPos
* SetComboBoxListBoxSelectIndex
* SetComponentClipArea
* SetComponentEnableClipArea
* SetComponentShowEditor
* SetComponentTextureAddressType
* SetFormCaptionPlusHeight
* SetFormClientColor
* SetFormEnableCloseButton
* SetFormFold
* SetFormGroupViewSpan
* SetFormHScrollPos
* SetFormMaxSize
* SetFormMinSize
* SetFormResizable
* SetFormVScrollPos
* SetUserComponentData
* SetUserComponentName

### Removed Functions

* RemoveActivityCloseParts
* RemoveAllActivityCloseParts

## Ixseal -> Rance X

### Signature Changes

* PlayPartsMovie
* AddPartsConstructionProcess
* GetPartsConstructionProcess
* UpdateComponent

### New Functions

* IsComboBoxOpenListBox
* IsEnableInputProcess
* IsEnableSpeedUp
* Parts\_IsHGaugeReverse
* Parts\_IsVGaugeReverse
* CreateUserPartsNumber
* GetPartsMovieCurrentTime
* GetPartsMovieEndTime
* Parts\_GetFlatBeginSeekFrame
* GetCheckBoxOverrideCGName
* GetCheckBoxOverrideCheckedCGName
* GetCheckBoxOverrideCheckedFlatName
* GetCheckBoxOverrideFlatName
* GetHScrollbarOverrideBarCGName
* GetHScrollbarOverrideBarFlatName
* GetHScrollbarOverrideBGCGName
* GetHScrollbarOverrideBGFlatName
* GetHScrollbarOverrideLeftCGName
* GetHScrollbarOverrideLeftFlatName
* GetHScrollbarOverrideRightCGName
* GetHScrollbarOverrideRightFlatName
* GetVScrollbarOverrideBarCGName
* GetVScrollbarOverrideBarFlatName
* GetVScrollbarOverrideBGCGName
* GetVScrollbarOverrideBGFlatName
* GetVScrollbarOverrideDownCGName
* GetVScrollbarOverrideDownFlatName
* GetVScrollbarOverrideUpCGName
* GetVScrollbarOverrideUpFlatName
* Parts\_SetFlatBeginSeekFrame
* Parts\_SetHGaugeReverse
* Parts\_SetVGaugeReverse
* ReleaseUserPartsNumber
* ResumeTexture
* SetCheckBoxOverrideCGName
* SetCheckBoxOverrideCheckedCGName
* SetCheckBoxOverrideCheckedFlatName
* SetCheckBoxOverrideFlatName
* SetEnableInputProcess
* SetEnableSpeedUp
* SetHScrollbarOverrideBarCGName
* SetHScrollbarOverrideBarFlatName
* SetHScrollbarOverrideBGCGName
* SetHScrollbarOverrideBGFlatName
* SetHScrollbarOverrideLeftCGName
* SetHScrollbarOverrideLeftFlatName
* SetHScrollbarOverrideRightCGName
* SetHScrollbarOverrideRightFlatName
* SetMovieTime
* SetVScrollbarOverrideBarCGName
* SetVScrollbarOverrideBarFlatName
* SetVScrollbarOverrideBGCGName
* SetVScrollbarOverrideBGFlatName
* SetVScrollbarOverrideDownCGName
* SetVScrollbarOverrideDownFlatName
* SetVScrollbarOverrideUpCGName
* SetVScrollbarOverrideUpFlatName
* SuspendTexture

### Removed Functions

* IsExistActivityIntentData
* Parts\_GetResetTimerByChangeInputStatus
* GetActivityIntentDataType
* GetComponentSpeedupRateByMessageSkip
* NumofActivityIntentDataDestination
* GetActivityIntentDataDestination
* AddActivityIntentDataDestination
* Parts\_SetResetTimerByChangeInputStatus
* ReleaseAllWithoutSystem
* SetActivityIntentData
* SetComponentSpeedupRateByMessageSkip

## Rance X -> Evenicle 2

### Signature Changes

* Save
  * Now takes a boolean "compress" argument
* AddPartsConstructionProcess

The following functions use the new ref type (called "wrap" currently in alice-tools) but are
otherwise identical:

* GetActivityParts
* LoadBackScene
* LoadParts
* Load
* ReleaseActivity
* SaveActivityEXText
* SaveBackScene
* SaveParts
* Debug\_GetSpriteRect
* GetButtonFontProperty
* GetCGDetectionSurfaceArea
* GetCheckBoxFontProperty
* GetComboBoxFontProperty
* GetComboBoxListBoxFontProperty
* GetComponentAbsolutePos
* GetComponentAbsoluteSquarePos
* GetConstructionSurfaceArea
* GetFlatSurfaceArea
* GetFormGroupViewSnap
* GetHGaugeSurfaceArea
* GetKeyWaitCGName
* GetListBoxFontProperty
* GetLoopCGSurfaceArea
* GetMessageWindowTextArea
* GetMessageWindowTextFont
* GetMessageWindowTextSpace
* GetMultiTextBoxFontProperty
* GetNumeralCGNumberWidthList
* GetNumeralFont
* GetNumeralSurfaceArea
* GetPartsCGSurfaceArea
* GetPartsConstructionProcess
* GetPartsTextFontProperty
* GetPartsTextSurfaceArea
* GetRectangleDetectionPoint
* GetRectangleDetectionSurfaceArea
* GetSpinBoxFontProperty
* GetTextBoxFontProperty
* GetVGaugeSurfaceArea
* Parts\_GetPartsSize
* Parts\_GetPartsUpperLeftPos
* Parts\_GetRubyFont
* RemoveController

### New Functions

* IsComponentSubColorMode
* IsEnableInput
* GetMessageUniqueID
* SeekMessage
* SetComponentSubColorMode
* SetEnableInput
* SetEventID

### Removed Functions

* GetMessageVariableCount
* SetDelegateIndex

## Evenicle 2 -> Haha Ranman

### Signature Changes

None.

### New Functions

* GetHSliderBarScrollRate
* GetVSliderBarScrollRate
* GetHSliderBarScrollPos
* GetHSliderBarTotalSize
* GetHSliderBarViewSize
* GetHSliderTotalSizeLinkNumber
* GetHSliderViewSizeLinkNumber
* GetMessageWindowRubyCharSpace
* GetMessageWindowRubyLineSpace
* GetMultiTextBoxMode
* GetVSliderBarScrollPos
* GetVSliderBarTotalSize
* GetVSliderBarViewSize
* GetVSliderTotalSizeLinkNumber
* GetVSliderViewSizeLinkNumber
* GetHSliderBarCGName
* GetHSliderBarFlatName
* GetHSliderBarOverrideBarCGName
* GetHSliderBarOverrideBarFlatName
* GetHSliderBarOverrideBGCGName
* GetHSliderBarOverrideBGFlatName
* GetVSliderBarCGName
* GetVSliderBarFlatName
* GetVSliderBarOverrideBarCGName
* GetVSliderBarOverrideBarFlatName
* GetVSliderBarOverrideBGCGName
* GetVSliderBarOverrideBGFlatName
* GetMessageWindowRubyFont
* SetHSliderBarCGName
* SetHSliderBarFlatName
* SetHSliderBarOverrideBarCGName
* SetHSliderBarOverrideBarFlatName
* SetHSliderBarOverrideBGCGName
* SetHSliderBarOverrideBGFlatName
* SetHSliderBarScrollPos
* SetHSliderBarScrollRate
* SetHSliderBarSize
* SetHSliderBarTotalSize
* SetHSliderBarViewSize
* SetHSliderTotalSizeLinkNumber
* SetHSliderViewSizeLinkNumber
* SetMessageWindowRubyCharSpace
* SetMessageWindowRubyFont
* SetMessageWindowRubyLineSpace
* SetMultiTextBoxMode
* SetVSliderBarCGName
* SetVSliderBarFlatName
* SetVSliderBarOverrideBarCGName
* SetVSliderBarOverrideBarFlatName
* SetVSliderBarOverrideBGCGName
* SetVSliderBarOverrideBGFlatName
* SetVSliderBarScrollPos
* SetVSliderBarScrollRate
* SetVSliderBarSize
* SetVSliderBarTotalSize
* SetVSliderBarViewSize
* SetVSliderTotalSizeLinkNumber
* SetVSliderViewSizeLinkNumber

### Removed Functions

None.

## Dohna Dohna

TODO

## HENTAI LABYRINTH

TODO

## Healing Touch

TODO
