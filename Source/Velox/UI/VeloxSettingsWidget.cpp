// Velox - settings screen (ADR 0004 section 7, task M3-10).

#include "UI/VeloxSettingsWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/VeloxSettingsMath.h"
#include "UI/VeloxUISettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxSettingsWidget)

namespace VeloxSettingsWidgetPrivate
{
	const TCHAR* const WindowModeNames[] = {TEXT("Fullscreen"), TEXT("Windowed fullscreen"), TEXT("Windowed")};
	const TCHAR* const CrosshairNames[] = {TEXT("Cross"), TEXT("Cross + dot"), TEXT("Dot")};
	const TCHAR* const AntiAliasingNames[] = {TEXT("MSAA 4x"), TEXT("FXAA"), TEXT("Off")};
	const TCHAR* const UnlimitedFrameRateName = TEXT("Unlimited");
	const TCHAR* const NoKeyName = TEXT("(unbound)");
	const TCHAR* const RebindPromptName = TEXT("press a key...");

	EWindowMode::Type WindowModeFromIndex(int32 Index)
	{
		switch (Index)
		{
		case 0:
			return EWindowMode::Fullscreen;
		case 1:
			return EWindowMode::WindowedFullscreen;
		default:
			return EWindowMode::Windowed;
		}
	}

	int32 IndexFromWindowMode(EWindowMode::Type Mode)
	{
		switch (Mode)
		{
		case EWindowMode::Fullscreen:
			return 0;
		case EWindowMode::WindowedFullscreen:
			return 1;
		default:
			return 2;
		}
	}

	FString ResolutionText(const FIntPoint& Resolution)
	{
		return FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y);
	}

	bool ParseResolution(const FString& Text, FIntPoint& Out)
	{
		FString Left;
		FString Right;
		if (!Text.Split(TEXT("x"), &Left, &Right) || !Left.IsNumeric() || !Right.IsNumeric())
		{
			return false;
		}
		Out = FIntPoint(FCString::Atoi(*Left), FCString::Atoi(*Right));
		return Out.X > 0 && Out.Y > 0;
	}

	FString FrameRateText(int32 Limit)
	{
		return Limit <= 0 ? FString(UnlimitedFrameRateName) : FString::FromInt(Limit);
	}

	FString KeyText(const FKey& Key)
	{
		return Key.IsValid() ? Key.GetDisplayName().ToString() : FString(NoKeyName);
	}

	template <typename ArrayT>
	void AddOptions(UComboBoxString* Combo, const ArrayT& Names)
	{
		for (const TCHAR* Name : Names)
		{
			Combo->AddOption(Name);
		}
	}
}

void UVeloxKeyRowProxy::HandleClicked()
{
	if (UVeloxSettingsWidget* Widget = Owner.Get())
	{
		Widget->BeginRebind(RowIndex);
	}
}

UVeloxSettingsWidget::UVeloxSettingsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Keyboard focus is needed for Escape and for the rebind capture (the preview handlers only see events routed through
	// this widget's Slate tree).
	SetIsFocusable(true);
}

UWidget* UVeloxSettingsWidget::BuildDefaultTree()
{
	using namespace VeloxSettingsWidgetPrivate;
	const UVeloxUISettings& UI = UVeloxUISettings::Get();

	UCanvasPanel* Root = MakeWidget<UCanvasPanel>(TEXT("Root"));
	UImage* Backdrop = MakeWidget<UImage>(TEXT("Backdrop"));
	Backdrop->SetBrush(FSlateColorBrush(FLinearColor::White));
	Backdrop->SetColorAndOpacity(UI.MenuBackgroundColor);
	if (UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop))
	{
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));
	}

	UBorder* Panel = MakeWidget<UBorder>(TEXT("Panel"));
	Panel->SetBrush(FSlateColorBrush(FLinearColor::White));
	Panel->SetBrushColor(UI.MenuPanelColor);
	Panel->SetPadding(FMargin(UI.MenuRowPadding * 3.f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5, 0.5));
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetPosition(FVector2D::ZeroVector);
	}

	UVerticalBox* Column = MakeWidget<UVerticalBox>(TEXT("Column"));
	Panel->SetContent(Column);
	Column->AddChildToVerticalBox(MakeText(TEXT("Title"), UI.MenuHeadingFontSize, UI.MenuTitleColor, TEXT("SETTINGS")));

	USizeBox* ScrollSize = MakeWidget<USizeBox>(TEXT("ScrollSize"));
	ScrollSize->SetWidthOverride(UI.SettingsPanelWidth);
	ScrollSize->SetMaxDesiredHeight(UI.SettingsPanelMaxHeight);
	UScrollBox* Scroll = MakeWidget<UScrollBox>(TEXT("Scroll"));
	ScrollSize->AddChild(Scroll);
	if (UVerticalBoxSlot* ScrollSlot = Column->AddChildToVerticalBox(ScrollSize))
	{
		ScrollSlot->SetPadding(FMargin(0.f, UI.MenuRowPadding));
	}
	UVerticalBox* List = MakeWidget<UVerticalBox>(TEXT("List"));
	Scroll->AddChild(List);

	auto AddHeading = [&](const TCHAR* Text)
	{
		if (UVerticalBoxSlot* HeadingSlot = List->AddChildToVerticalBox(MakeText(NAME_None, UI.MenuHeadingFontSize - 6, UI.MenuTitleColor, Text)))
		{
			HeadingSlot->SetPadding(FMargin(0.f, UI.MenuRowPadding * 3.f, 0.f, UI.MenuRowPadding));
		}
	};
	auto AddRow = [&](const TCHAR* Label, UWidget* Control, UTextBlock* ValueText)
	{
		UHorizontalBox* Row = MakeWidget<UHorizontalBox>(NAME_None);
		USizeBox* LabelBox = MakeWidget<USizeBox>(NAME_None);
		LabelBox->SetWidthOverride(UI.SettingsLabelWidth);
		LabelBox->AddChild(MakeText(NAME_None, UI.MenuFontSize, UI.MenuTextColor, Label));
		Row->AddChildToHorizontalBox(LabelBox);
		USizeBox* ControlBox = MakeWidget<USizeBox>(NAME_None);
		ControlBox->SetWidthOverride(UI.SettingsControlWidth);
		ControlBox->AddChild(Control);
		if (UHorizontalBoxSlot* ControlSlot = Row->AddChildToHorizontalBox(ControlBox))
		{
			ControlSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (ValueText)
		{
			if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(ValueText))
			{
				ValueSlot->SetPadding(FMargin(UI.MenuRowPadding * 2.f, 0.f));
				ValueSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
		if (UVerticalBoxSlot* RowSlot = List->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(0.f, UI.MenuRowPadding * 0.5f));
		}
	};
	auto MakeSlider = [&](FName Name, float Min, float Max)
	{
		USlider* Slider = MakeWidget<USlider>(Name);
		Slider->SetMinValue(Min);
		Slider->SetMaxValue(Max);
		return Slider;
	};
	auto MakeValue = [&](FName Name)
	{
		return MakeText(Name, UI.MenuFontSize, UI.MenuMutedTextColor, FString());
	};
	auto MakeCheck = [&](FName Name)
	{
		return MakeWidget<UCheckBox>(Name);
	};
	auto MakeCombo = [&](FName Name)
	{
		return MakeWidget<UComboBoxString>(Name);
	};

	AddHeading(TEXT("Player"));
	PlayerNameBox = MakeWidget<UEditableTextBox>(TEXT("PlayerNameBox"));
	AddRow(TEXT("Name"), PlayerNameBox, nullptr);

	AddHeading(TEXT("Mouse and view"));
	SensitivitySlider = MakeSlider(TEXT("SensitivitySlider"), VeloxSettingsMath::MinSensitivity, VeloxSettingsMath::MaxSensitivity);
	SensitivityValue = MakeValue(TEXT("SensitivityValue"));
	AddRow(TEXT("Mouse sensitivity"), SensitivitySlider, SensitivityValue);
	InvertYCheck = MakeCheck(TEXT("InvertYCheck"));
	AddRow(TEXT("Invert mouse Y"), InvertYCheck, nullptr);
	FovSlider = MakeSlider(TEXT("FovSlider"), VeloxSettingsMath::MinFieldOfView, VeloxSettingsMath::MaxFieldOfView);
	FovValue = MakeValue(TEXT("FovValue"));
	AddRow(TEXT("Field of view"), FovSlider, FovValue);
	AutoSwitchCheck = MakeCheck(TEXT("AutoSwitchCheck"));
	AddRow(TEXT("Switch to picked-up weapons"), AutoSwitchCheck, nullptr);
	CrosshairStyleCombo = MakeCombo(TEXT("CrosshairStyleCombo"));
	AddOptions(CrosshairStyleCombo, CrosshairNames);
	AddRow(TEXT("Crosshair"), CrosshairStyleCombo, nullptr);
	CrosshairSizeSlider = MakeSlider(TEXT("CrosshairSizeSlider"), VeloxSettingsMath::MinCrosshairSize, VeloxSettingsMath::MaxCrosshairSize);
	CrosshairSizeValue = MakeValue(TEXT("CrosshairSizeValue"));
	AddRow(TEXT("Crosshair size"), CrosshairSizeSlider, CrosshairSizeValue);

	AddHeading(TEXT("Audio"));
	MasterVolumeSlider = MakeSlider(TEXT("MasterVolumeSlider"), 0.f, 1.f);
	MasterVolumeValue = MakeValue(TEXT("MasterVolumeValue"));
	AddRow(TEXT("Master volume"), MasterVolumeSlider, MasterVolumeValue);
	EffectsVolumeSlider = MakeSlider(TEXT("EffectsVolumeSlider"), 0.f, 1.f);
	EffectsVolumeValue = MakeValue(TEXT("EffectsVolumeValue"));
	AddRow(TEXT("Effects volume"), EffectsVolumeSlider, EffectsVolumeValue);
	MusicVolumeSlider = MakeSlider(TEXT("MusicVolumeSlider"), 0.f, 1.f);
	MusicVolumeValue = MakeValue(TEXT("MusicVolumeValue"));
	AddRow(TEXT("Music volume"), MusicVolumeSlider, MusicVolumeValue);

	AddHeading(TEXT("Video"));
	WindowModeCombo = MakeCombo(TEXT("WindowModeCombo"));
	AddOptions(WindowModeCombo, WindowModeNames);
	AddRow(TEXT("Window mode"), WindowModeCombo, nullptr);
	ResolutionCombo = MakeCombo(TEXT("ResolutionCombo"));
	AddRow(TEXT("Resolution"), ResolutionCombo, nullptr);
	VSyncCheck = MakeCheck(TEXT("VSyncCheck"));
	AddRow(TEXT("VSync"), VSyncCheck, nullptr);
	FrameRateCombo = MakeCombo(TEXT("FrameRateCombo"));
	AddRow(TEXT("Frame rate limit"), FrameRateCombo, nullptr);
	AntiAliasingCombo = MakeCombo(TEXT("AntiAliasingCombo"));
	AddOptions(AntiAliasingCombo, AntiAliasingNames);
	AddRow(TEXT("Anti-aliasing"), AntiAliasingCombo, nullptr);

	AddHeading(TEXT("Key bindings (click a key, then press the new one)"));
	KeyList = MakeWidget<UVerticalBox>(TEXT("KeyList"));
	List->AddChildToVerticalBox(KeyList);

	StatusText = MakeText(TEXT("StatusText"), UI.MenuFontSize, UI.MenuTextColor, FString());
	if (UVerticalBoxSlot* StatusSlot = Column->AddChildToVerticalBox(StatusText))
	{
		StatusSlot->SetPadding(FMargin(0.f, UI.MenuRowPadding));
	}

	UHorizontalBox* Buttons = MakeWidget<UHorizontalBox>(TEXT("Buttons"));
	ApplyButton = MakeButton(TEXT("ApplyButton"), TEXT("Apply"));
	DefaultsButton = MakeButton(TEXT("DefaultsButton"), TEXT("Defaults"));
	BackButton = MakeButton(TEXT("BackButton"), TEXT("Back"));
	for (UButton* Button : {ApplyButton.Get(), DefaultsButton.Get(), BackButton.Get()})
	{
		if (UHorizontalBoxSlot* ButtonSlot = Buttons->AddChildToHorizontalBox(Button))
		{
			ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			ButtonSlot->SetPadding(FMargin(UI.MenuRowPadding));
		}
	}
	Column->AddChildToVerticalBox(Buttons);
	return Root;
}

void UVeloxSettingsWidget::BindElements()
{
	for (USlider* Slider : {SensitivitySlider.Get(), FovSlider.Get(), CrosshairSizeSlider.Get(), MasterVolumeSlider.Get(), EffectsVolumeSlider.Get(), MusicVolumeSlider.Get()})
	{
		if (Slider)
		{
			Slider->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleSliderChanged);
		}
	}
	if (ApplyButton)
	{
		ApplyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleApply);
	}
	if (DefaultsButton)
	{
		DefaultsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleDefaults);
	}
	if (BackButton)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBack);
	}
}

void UVeloxSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BindElements();
	ReadFromSettings(UVeloxGameUserSettings::GetOrDefault());
	RebuildKeyRows();
	SetStatus(FString(), false);
}

void UVeloxSettingsWidget::HandleSliderChanged(float Value)
{
	RefreshValueTexts();
}

void UVeloxSettingsWidget::HandleApply()
{
	ApplyChanges();
}

void UVeloxSettingsWidget::HandleDefaults()
{
	RestoreDefaults();
}

void UVeloxSettingsWidget::HandleBack()
{
	Close();
}

void UVeloxSettingsWidget::RefreshValueTexts()
{
	auto SetValue = [](UTextBlock* Text, const FString& Value)
	{
		if (Text)
		{
			Text->SetText(FText::FromString(Value));
		}
	};
	if (SensitivitySlider)
	{
		SetValue(SensitivityValue, FString::Printf(TEXT("%.1f"), SensitivitySlider->GetValue()));
	}
	if (FovSlider)
	{
		SetValue(FovValue, FString::Printf(TEXT("%.0f"), FovSlider->GetValue()));
	}
	if (CrosshairSizeSlider)
	{
		SetValue(CrosshairSizeValue, FString::Printf(TEXT("%.0f"), CrosshairSizeSlider->GetValue()));
	}
	if (MasterVolumeSlider)
	{
		SetValue(MasterVolumeValue, FString::Printf(TEXT("%.0f%%"), MasterVolumeSlider->GetValue() * 100.f));
	}
	if (EffectsVolumeSlider)
	{
		SetValue(EffectsVolumeValue, FString::Printf(TEXT("%.0f%%"), EffectsVolumeSlider->GetValue() * 100.f));
	}
	if (MusicVolumeSlider)
	{
		SetValue(MusicVolumeValue, FString::Printf(TEXT("%.0f%%"), MusicVolumeSlider->GetValue() * 100.f));
	}
}

void UVeloxSettingsWidget::FillResolutionOptions(const FIntPoint& Current)
{
	using namespace VeloxSettingsWidgetPrivate;
	if (!ResolutionCombo)
	{
		return;
	}
	TArray<FIntPoint> Resolutions;
	if (!UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions) || Resolutions.IsEmpty())
	{
		UKismetSystemLibrary::GetConvenientWindowedResolutions(Resolutions);
	}
	Resolutions.AddUnique(Current);
	Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	ResolutionCombo->ClearOptions();
	for (const FIntPoint& Resolution : Resolutions)
	{
		ResolutionCombo->AddOption(ResolutionText(Resolution));
	}
}

void UVeloxSettingsWidget::FillFrameRateOptions(float CurrentLimit)
{
	using namespace VeloxSettingsWidgetPrivate;
	if (!FrameRateCombo)
	{
		return;
	}
	TArray<int32> Limits = UVeloxUISettings::Get().FrameRateLimitChoices;
	Limits.AddUnique(FMath::Max(0, FMath::RoundToInt(CurrentLimit)));
	Limits.Sort();
	FrameRateCombo->ClearOptions();
	for (const int32 Limit : Limits)
	{
		FrameRateCombo->AddOption(FrameRateText(Limit));
	}
}

void UVeloxSettingsWidget::ReadFromSettings(const UVeloxGameUserSettings& Settings, bool bKeepIdentity)
{
	using namespace VeloxSettingsWidgetPrivate;
	if (SensitivitySlider)
	{
		SensitivitySlider->SetValue(Settings.MouseSensitivity);
	}
	if (InvertYCheck)
	{
		InvertYCheck->SetIsChecked(Settings.bInvertMouseY);
	}
	if (FovSlider)
	{
		FovSlider->SetValue(Settings.FieldOfView);
	}
	if (AutoSwitchCheck)
	{
		AutoSwitchCheck->SetIsChecked(Settings.bAutoSwitchOnPickup);
	}
	if (PlayerNameBox && !bKeepIdentity)
	{
		PlayerNameBox->SetText(FText::FromString(Settings.PlayerName));
	}
	if (CrosshairStyleCombo)
	{
		CrosshairStyleCombo->SetSelectedIndex(static_cast<int32>(Settings.CrosshairStyle));
	}
	if (CrosshairSizeSlider)
	{
		CrosshairSizeSlider->SetValue(Settings.CrosshairSize);
	}
	if (MasterVolumeSlider)
	{
		MasterVolumeSlider->SetValue(Settings.MasterVolume);
	}
	if (EffectsVolumeSlider)
	{
		EffectsVolumeSlider->SetValue(Settings.EffectsVolume);
	}
	if (MusicVolumeSlider)
	{
		MusicVolumeSlider->SetValue(Settings.MusicVolume);
	}
	if (AntiAliasingCombo)
	{
		AntiAliasingCombo->SetSelectedIndex(static_cast<int32>(Settings.AntiAliasing));
	}
	if (WindowModeCombo)
	{
		WindowModeCombo->SetSelectedIndex(IndexFromWindowMode(Settings.GetFullscreenMode()));
	}
	if (ResolutionCombo)
	{
		const FIntPoint Resolution = Settings.GetScreenResolution();
		FillResolutionOptions(Resolution);
		ResolutionCombo->SetSelectedOption(ResolutionText(Resolution));
	}
	if (VSyncCheck)
	{
		VSyncCheck->SetIsChecked(Settings.IsVSyncEnabled());
	}
	if (FrameRateCombo)
	{
		FillFrameRateOptions(Settings.GetFrameRateLimit());
		FrameRateCombo->SetSelectedOption(FrameRateText(FMath::Max(0, FMath::RoundToInt(Settings.GetFrameRateLimit()))));
	}
	RefreshValueTexts();
}

void UVeloxSettingsWidget::WriteToSettings(UVeloxGameUserSettings& Settings) const
{
	using namespace VeloxSettingsWidgetPrivate;
	if (SensitivitySlider)
	{
		Settings.MouseSensitivity = SensitivitySlider->GetValue();
	}
	if (InvertYCheck)
	{
		Settings.bInvertMouseY = InvertYCheck->IsChecked();
	}
	if (FovSlider)
	{
		Settings.FieldOfView = FovSlider->GetValue();
	}
	if (AutoSwitchCheck)
	{
		Settings.bAutoSwitchOnPickup = AutoSwitchCheck->IsChecked();
	}
	if (PlayerNameBox)
	{
		Settings.PlayerName = VeloxSettingsMath::SanitizePlayerName(PlayerNameBox->GetText().ToString());
	}
	if (CrosshairStyleCombo && CrosshairStyleCombo->GetSelectedIndex() >= 0)
	{
		Settings.CrosshairStyle = static_cast<EVeloxCrosshairStyle>(FMath::Clamp(CrosshairStyleCombo->GetSelectedIndex(), 0, static_cast<int32>(UE_ARRAY_COUNT(CrosshairNames)) - 1));
	}
	if (CrosshairSizeSlider)
	{
		Settings.CrosshairSize = CrosshairSizeSlider->GetValue();
	}
	if (MasterVolumeSlider)
	{
		Settings.MasterVolume = MasterVolumeSlider->GetValue();
	}
	if (EffectsVolumeSlider)
	{
		Settings.EffectsVolume = EffectsVolumeSlider->GetValue();
	}
	if (MusicVolumeSlider)
	{
		Settings.MusicVolume = MusicVolumeSlider->GetValue();
	}
	if (AntiAliasingCombo && AntiAliasingCombo->GetSelectedIndex() >= 0)
	{
		Settings.AntiAliasing = static_cast<EVeloxAntiAliasing>(FMath::Clamp(AntiAliasingCombo->GetSelectedIndex(), 0, static_cast<int32>(UE_ARRAY_COUNT(AntiAliasingNames)) - 1));
	}
	if (WindowModeCombo && WindowModeCombo->GetSelectedIndex() >= 0)
	{
		Settings.SetFullscreenMode(WindowModeFromIndex(WindowModeCombo->GetSelectedIndex()));
	}
	FIntPoint Resolution;
	if (ResolutionCombo && ParseResolution(ResolutionCombo->GetSelectedOption(), Resolution))
	{
		Settings.SetScreenResolution(Resolution);
	}
	if (VSyncCheck)
	{
		Settings.SetVSyncEnabled(VSyncCheck->IsChecked());
	}
	if (FrameRateCombo && FrameRateCombo->GetSelectedIndex() >= 0)
	{
		Settings.SetFrameRateLimit(static_cast<float>(FCString::Atoi(*FrameRateCombo->GetSelectedOption())));
	}
}

void UVeloxSettingsWidget::ApplyChanges()
{
	UVeloxGameUserSettings* Settings = UVeloxGameUserSettings::Get();
	if (!Settings)
	{
		SetStatus(TEXT("Settings are unavailable."), true);
		return;
	}
	WriteToSettings(*Settings);
	// Resolution, window mode, vsync and frame limit (saves too), then ours: AA, audio, clamps; written once more at the end.
	Settings->ApplySettings(false);
	Settings->ApplyAndSaveVeloxSettings(GetWorld());
	ReadFromSettings(*Settings);
	SetStatus(TEXT("Settings applied and saved."), false);
}

void UVeloxSettingsWidget::RestoreDefaults()
{
	UVeloxGameUserSettings* Defaults = NewObject<UVeloxGameUserSettings>(GetTransientPackage());
	Defaults->SetToDefaults();
	ReadFromSettings(*Defaults, true);
	UVeloxGameUserSettings::ResetKeyBindings(GetWorld());
	RebuildKeyRows();
	SetStatus(TEXT("Defaults restored. Key bindings are reset; press Apply to keep the other values."), false);
}

void UVeloxSettingsWidget::Close()
{
	CancelRebind();
	RemoveFromParent();
	OnClosed.Broadcast();
}

void UVeloxSettingsWidget::SetStatus(const FString& Text, bool bError)
{
	if (StatusText)
	{
		const UVeloxUISettings& UI = UVeloxUISettings::Get();
		StatusText->SetText(FText::FromString(Text));
		StatusText->SetColorAndOpacity(FSlateColor(bError ? UI.MenuErrorColor : UI.MenuTextColor));
	}
}

void UVeloxSettingsWidget::RebuildKeyRows()
{
	using namespace VeloxSettingsWidgetPrivate;
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	KeyRows.Reset();
	KeyProxies.Reset();
	RebindRow = INDEX_NONE;
	if (KeyList)
	{
		KeyList->ClearChildren();
	}
	TArray<FVeloxKeyBinding> Bindings;
	UVeloxGameUserSettings::GetKeyBindings(GetWorld(), Bindings);
	for (const FVeloxKeyBinding& Binding : Bindings)
	{
		FKeyRow& Row = KeyRows.AddDefaulted_GetRef();
		Row.MappingName = Binding.MappingName;
		Row.DisplayName = Binding.DisplayName.ToString();
		Row.CurrentKey = Binding.CurrentKey;
		if (!KeyList)
		{
			continue;
		}
		const int32 RowIndex = KeyRows.Num() - 1;
		UVeloxKeyRowProxy* Proxy = NewObject<UVeloxKeyRowProxy>(this);
		Proxy->Owner = this;
		Proxy->RowIndex = RowIndex;
		KeyProxies.Add(Proxy);

		UHorizontalBox* Line = MakeWidget<UHorizontalBox>(NAME_None);
		USizeBox* LabelBox = MakeWidget<USizeBox>(NAME_None);
		LabelBox->SetWidthOverride(UI.SettingsLabelWidth);
		LabelBox->AddChild(MakeText(NAME_None, UI.MenuFontSize, UI.MenuTextColor, Row.DisplayName));
		Line->AddChildToHorizontalBox(LabelBox);
		UButton* KeyButton = MakeButton(NAME_None, KeyText(Binding.CurrentKey));
		KeyButton->OnClicked.AddDynamic(Proxy, &UVeloxKeyRowProxy::HandleClicked);
		Row.KeyLabel = GetButtonLabel(KeyButton);
		USizeBox* ButtonBox = MakeWidget<USizeBox>(NAME_None);
		ButtonBox->SetWidthOverride(UI.SettingsControlWidth);
		ButtonBox->AddChild(KeyButton);
		Line->AddChildToHorizontalBox(ButtonBox);
		if (UVerticalBoxSlot* LineSlot = KeyList->AddChildToVerticalBox(Line))
		{
			LineSlot->SetPadding(FMargin(0.f, UI.MenuRowPadding * 0.5f));
		}
	}
	if (KeyList && Bindings.IsEmpty())
	{
		KeyList->AddChildToVerticalBox(MakeText(NAME_None, UI.MenuFontSize, UI.MenuMutedTextColor, TEXT("No key bindings available.")));
	}
}

FString UVeloxSettingsWidget::DescribeKeyRow(int32 RowIndex) const
{
	using namespace VeloxSettingsWidgetPrivate;
	return KeyRows.IsValidIndex(RowIndex) ? FString::Printf(TEXT("%s=%s"), *KeyRows[RowIndex].DisplayName, *KeyText(KeyRows[RowIndex].CurrentKey)) : FString();
}

bool UVeloxSettingsWidget::BeginRebind(int32 RowIndex)
{
	using namespace VeloxSettingsWidgetPrivate;
	if (!KeyRows.IsValidIndex(RowIndex))
	{
		return false;
	}
	CancelRebind();
	RebindRow = RowIndex;
	if (UTextBlock* Label = KeyRows[RowIndex].KeyLabel.Get())
	{
		Label->SetText(FText::FromString(RebindPromptName));
	}
	SetStatus(FString::Printf(TEXT("Press a key or mouse button for \"%s\" (Escape cancels)."), *KeyRows[RowIndex].DisplayName), false);
	SetKeyboardFocus();
	return true;
}

void UVeloxSettingsWidget::CancelRebind()
{
	using namespace VeloxSettingsWidgetPrivate;
	if (KeyRows.IsValidIndex(RebindRow))
	{
		if (UTextBlock* Label = KeyRows[RebindRow].KeyLabel.Get())
		{
			Label->SetText(FText::FromString(KeyText(KeyRows[RebindRow].CurrentKey)));
		}
	}
	RebindRow = INDEX_NONE;
}

bool UVeloxSettingsWidget::CommitRebind(const FKey& Key)
{
	using namespace VeloxSettingsWidgetPrivate;
	if (!KeyRows.IsValidIndex(RebindRow) || !Key.IsValid())
	{
		return false;
	}
	FKeyRow& Row = KeyRows[RebindRow];
	FString Failure;
	const bool bOk = UVeloxGameUserSettings::RebindKey(GetWorld(), Row.MappingName, Key, Failure);
	if (bOk)
	{
		Row.CurrentKey = Key;
		SetStatus(FString::Printf(TEXT("\"%s\" is now %s."), *Row.DisplayName, *KeyText(Key)), false);
	}
	else
	{
		SetStatus(FString::Printf(TEXT("Could not bind %s: %s"), *KeyText(Key), *Failure), true);
	}
	CancelRebind();
	return bOk;
}

FReply UVeloxSettingsWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (IsRebinding())
	{
		if (Key == EKeys::Escape)
		{
			CancelRebind();
			SetStatus(FString(), false);
		}
		else
		{
			CommitRebind(Key);
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UVeloxSettingsWidget::NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (IsRebinding())
	{
		CommitRebind(InMouseEvent.GetEffectingButton());
		return FReply::Handled();
	}
	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

FString UVeloxSettingsWidget::DescribeValues() const
{
	using namespace VeloxSettingsWidgetPrivate;
	return FString::Printf(
		TEXT("sens=%.2f invert=%d fov=%.0f autoswitch=%d name=%s crosshair=%s/%.0f vol=%.2f/%.2f/%.2f aa=%s window=%s res=%s vsync=%d fps=%s keys=%d"),
		SensitivitySlider ? SensitivitySlider->GetValue() : -1.f, InvertYCheck && InvertYCheck->IsChecked() ? 1 : 0,
		FovSlider ? FovSlider->GetValue() : -1.f, AutoSwitchCheck && AutoSwitchCheck->IsChecked() ? 1 : 0,
		PlayerNameBox ? *PlayerNameBox->GetText().ToString() : TEXT("-"),
		CrosshairStyleCombo ? *CrosshairStyleCombo->GetSelectedOption() : TEXT("-"), CrosshairSizeSlider ? CrosshairSizeSlider->GetValue() : -1.f,
		MasterVolumeSlider ? MasterVolumeSlider->GetValue() : -1.f, EffectsVolumeSlider ? EffectsVolumeSlider->GetValue() : -1.f,
		MusicVolumeSlider ? MusicVolumeSlider->GetValue() : -1.f, AntiAliasingCombo ? *AntiAliasingCombo->GetSelectedOption() : TEXT("-"),
		WindowModeCombo ? *WindowModeCombo->GetSelectedOption() : TEXT("-"), ResolutionCombo ? *ResolutionCombo->GetSelectedOption() : TEXT("-"),
		VSyncCheck && VSyncCheck->IsChecked() ? 1 : 0, FrameRateCombo ? *FrameRateCombo->GetSelectedOption() : TEXT("-"), KeyRows.Num());
}
