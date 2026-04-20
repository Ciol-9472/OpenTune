#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstring>
#include <functional>

namespace OpenTune {

enum class Language
{
    English = 0,
    Chinese,
    Japanese,
    Russian,
    Spanish,
    Count
};

inline juce::String getLanguageName(Language lang)
{
    switch (lang)
    {
        case Language::English:  return "English";
        case Language::Chinese:  return "中文";
        case Language::Japanese: return "日本語";
        case Language::Russian:  return "Русский";
        case Language::Spanish:  return "Español";
        default: return "English";
    }
}

inline juce::String getLanguageNativeName(Language lang)
{
    switch (lang)
    {
        case Language::English:  return juce::String::fromUTF8("English");
        case Language::Chinese:  return juce::String::fromUTF8("\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87");  // 简体中文
        case Language::Japanese: return juce::String::fromUTF8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");  // 日本語
        case Language::Russian:  return juce::String::fromUTF8("\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9");  // Русский
        case Language::Spanish:  return juce::String::fromUTF8("Espa\xcf\x81ol");  // Español
        default: return juce::String::fromUTF8("English");
    }
}

// LanguageChangeListener 接口 - 观察者模式
class LanguageChangeListener
{
public:
    virtual ~LanguageChangeListener() = default;
    virtual void languageChanged(Language newLanguage) = 0;
};

class LocalizationManager
{
public:
    static LocalizationManager& getInstance()
    {
        static LocalizationManager instance;
        return instance;
    }

    void setLanguage(Language lang)
    {
        if (currentLanguage_ != lang)
        {
            currentLanguage_ = lang;
            // 广播给所有监听者
            listeners_.call(&LanguageChangeListener::languageChanged, lang);
        }
    }

    Language getLanguage() const { return currentLanguage_; }

    void addListener(LanguageChangeListener* listener) { listeners_.add(listener); }
    void removeListener(LanguageChangeListener* listener) { listeners_.remove(listener); }

private:
    LocalizationManager() = default;
    Language currentLanguage_ = Language::Chinese;
    juce::ListenerList<LanguageChangeListener> listeners_;
};

namespace Loc {

template<typename... Args>
juce::String tr(const char* key, Args... args)
{
    return juce::String::fromUTF8(key);
}

namespace Keys {

constexpr const char* kFile = "File";
constexpr const char* kEdit = "Edit";
constexpr const char* kView = "View";

constexpr const char* kImportAudio = "Import Audio...";
constexpr const char* kExportAudio = "Export Audio";
constexpr const char* kExportSelectedClip = "Export Selected Clip";
constexpr const char* kExportTrack = "Export Track";
constexpr const char* kExportBus = "Export Bus (Master Mix)";
constexpr const char* kExportStems = "Export Stems...";
constexpr const char* kExportStemsTitle = "Export Stems";
constexpr const char* kExportStemsPrefixLabel = "File name prefix:";
constexpr const char* kExportStemsTracksLabel = "Tracks:";
constexpr const char* kExportStemsChooseFolder = "Select folder for stem export";
constexpr const char* kExportStemsPickTrackWarning = "Please select at least one track that contains audio.";
constexpr const char* kExportStemsCompleteTitle = "Export Complete";
constexpr const char* kExportStemsCompleteMessage = "Exported {0} file(s) to:\n{1}";
constexpr const char* kExportStemsFailedTitle = "Stem export failed";
constexpr const char* kExportAudioCompleteMessage = "{0}\nExported to:\n{1}";
constexpr const char* kExportAudioFailedTitle = "Export Failed";
constexpr const char* kExportAudioFailedMessage = "Unable to export audio to:\n{0}";
constexpr const char* kExportAudioFailedReason = "\nReason: {0}";
constexpr const char* kOk = "OK";
constexpr const char* kSaveProject = "Save Project...";
constexpr const char* kSaveProjectAs = "Save Project As...";
constexpr const char* kLoadProject = "Open Project...";
constexpr const char* kNewProject = "New Project";
constexpr const char* kUnsavedChangesTitle = "Unsaved changes";
constexpr const char* kUnsavedChangesMessage = "Save changes to the current project before creating a new one?";
constexpr const char* kUnsavedChangesLoadMessage = "Save changes to the current project before opening another one?";
constexpr const char* kSave = "Save";
constexpr const char* kDontSave = "Don't Save";
constexpr const char* kProjectSavedTitle = "Project Saved";
constexpr const char* kProjectSavedMessage = "Project saved to:\n{0}";
constexpr const char* kProjectSaveFailedTitle = "Save Failed";
constexpr const char* kProjectSaveFailedMessage = "Failed to save project.";
constexpr const char* kProjectLoadedTitle = "Project Loaded";
constexpr const char* kProjectLoadedMessage = "Project loaded successfully.";
constexpr const char* kProjectLoadFailedTitle = "Load Failed";
constexpr const char* kProjectLoadFailedMessage = "Failed to load project or invalid file.";
constexpr const char* kRecentProjects = "Recent Projects";
constexpr const char* kRecentProjectsEmpty = "No recent projects";
constexpr const char* kOptions = "Options";
constexpr const char* kStandaloneAudioOutputSampleRate = "Output sample rate";
constexpr const char* kStandaloneAudioSampleRateDriverDefault = "Driver default";

constexpr const char* kUndo = "Undo";
constexpr const char* kRedo = "Redo";
constexpr const char* kUndoToMenuTitle = "Undo to";
constexpr const char* kUndoToMenuEmpty = "No actions available";

constexpr const char* kShowWaveform = "Show Waveform";
constexpr const char* kShowLanes = "Show Lanes";
constexpr const char* kNoteNames = "Note Names";
constexpr const char* kShowAllNotes = "Show All Notes";
constexpr const char* kShowCOnly = "Show C Only";
constexpr const char* kHideNoteNames = "Hide Note Names";
constexpr const char* kShowNoteBlockNoteNames = "Note Names on Blocks";
constexpr const char* kTheme = "Theme";
constexpr const char* kThemeBlueBreeze = "Blue Breeze";
constexpr const char* kThemeDarkBlueGrey = "Dark Blue-Grey";
constexpr const char* kThemeAurora = "Aurora Glass";

constexpr const char* kMouseTrail = "Mouse Trail";
constexpr const char* kOff = "Off";
constexpr const char* kClassic = "Classic";
constexpr const char* kNeon = "Neon";
constexpr const char* kFire = "Fire";
constexpr const char* kOcean = "Ocean";
constexpr const char* kGalaxy = "Galaxy";
constexpr const char* kCherryBlossom = "Cherry Blossom";
constexpr const char* kMatrix = "Matrix";

constexpr const char* kAudio = "Audio";
constexpr const char* kMouse = "Mouse";
constexpr const char* kKeyswitch = "Keyswitch";
constexpr const char* kLanguage = "Language";
constexpr const char* kLanguageLabel = "Interface Language";

constexpr const char* kHorizontalZoomSensitivity = "Horizontal Zoom Sensitivity";
constexpr const char* kVerticalZoomSensitivity = "Vertical Zoom Sensitivity";
constexpr const char* kScrollSpeed = "Scroll Speed";
constexpr const char* kResetToDefaults = "Reset to Defaults";

constexpr const char* kSetShortcut = "Set Shortcut";
constexpr const char* kPressNewKeyCombination = "Press the new key combination";
constexpr const char* kCurrent = "Current";
constexpr const char* kCancel = "Cancel";
constexpr const char* kShortcutConflict = "Shortcut Conflict";
constexpr const char* kShortcutConflictMessage = "This shortcut is already assigned to \"{0}\".\n\nDo you want to reassign it?";
constexpr const char* kYes = "Yes";
constexpr const char* kNo = "No";
constexpr const char* kResetAllToDefaults = "Reset All to Defaults";

/** Top-level menu titles: name + (Alt mnemonic letter), not part of KeyShortcutConfig. */
constexpr const char* kMenuBarTitleFile = "File (F)";
constexpr const char* kMenuBarTitleEdit = "Edit (E)";
constexpr const char* kMenuBarTitleView = "View (V)";
constexpr const char* kMenuBarTitleHelp = "Help (H)";
constexpr const char* kOpenSourceRepository = "Source repository";
constexpr const char* kShortcutGroupTransport = "Transport";
constexpr const char* kShortcutGroupProject = "Project";
constexpr const char* kShortcutGroupExport = "Export";
constexpr const char* kShortcutGroupEdit = "Edit";
constexpr const char* kShortcutGroupPianoRoll = "Piano roll & arrangement";
constexpr const char* kShortcutGroupView = "View & options";
constexpr const char* kShortcutGroupHelp = "Help";

constexpr const char* kPlayPause = "Play/Pause";
constexpr const char* kStop = "Stop";
constexpr const char* kPlayFromStart = "Play from Start";
constexpr const char* kCut = "Cut";
constexpr const char* kCopy = "Copy";
constexpr const char* kPaste = "Paste";
constexpr const char* kSelectAll = "Select All";
constexpr const char* kDelete = "Delete";

constexpr const char* kPitchCorrection = "Pitch correction";
constexpr const char* kRetuneSpeed = "Retune Speed";
constexpr const char* kVibratoDepth = "Vib. Depth";
constexpr const char* kVibratoRate = "Vib. Rate";
constexpr const char* kNoteSplit = "Note Split";
constexpr const char* kTools = "Tools";
constexpr const char* kAuto = "Auto";
constexpr const char* kSelect = "Select";
constexpr const char* kDrawNotes = "Draw notes";
constexpr const char* kLineAnchor = "Line anchor";
constexpr const char* kHandDraw = "Hand draw pitch";
constexpr const char* kSplitNote = "Split note";
constexpr const char* kAutoOptionsTitle = "Auto Pitch Detection Options";
constexpr const char* kAutoOverwriteWarning = "AUTO overwrite warning";
constexpr const char* kAutoUseAsDefaultAndSkip = "AUTO use as default and skip prompt";
constexpr const char* kAutoRightClickOptions = "AUTO right click options";

constexpr const char* kPlay = "Play";
constexpr const char* kPause = "Pause";
constexpr const char* kLoop = "Loop";
constexpr const char* kTapTempo = "Tap Tempo";
/** Transport: bypass corrected signal, hear dry/original */
constexpr const char* kTransportBypass = "Bypass pitch correction";
constexpr const char* kTransportTimeDisplay = "Position; click to switch time / bars";
constexpr const char* kTransportBpm = "Tempo (BPM)";
constexpr const char* kTransportScaleRoot = "Key (root note)";
constexpr const char* kTransportScaleType = "Scale type";
constexpr const char* kTrackView = "Track View";
constexpr const char* kPianoRollView = "Piano Roll View";

constexpr const char* kTracks = "Tracks";
constexpr const char* kProps = "Props";
constexpr const char* kScale = "Scale";

        constexpr const char* kClose = "Close";
        constexpr const char* kHelp = "Help...";
/** Settings / shortcut list: split clip in arrangement (keyboard). */
constexpr const char* kShortcutSplitClip = "Split clip (Arrangement)";
/** Arrangement clip context menu */
constexpr const char* kArrangementSplitAtPlayhead = "Split at playhead";
constexpr const char* kArrangementMergeWithNextClip = "Merge with next clip";
constexpr const char* kArrangementRenameClip = "Rename clip...";
constexpr const char* kTrackInsert = "Insert Track";
constexpr const char* kTrackDelete = "Delete Track";
constexpr const char* kRenameClipDialogTitle = "Rename clip";
constexpr const char* kRenameClipPrompt = "Enter a new name for this clip:";
constexpr const char* kRenameClipEditorLabel = "Name";
/** Toolbar tooltip: prefix before key binding display */
constexpr const char* kToolbarTooltipShortcut = "Shortcut:";
/** AUTO: current clip already has notes */
constexpr const char* kAutoTuneNotesPresentTitle = "AUTO";
constexpr const char* kAutoTuneNotesPresentMessage =
    "This clip already has notes. AUTO will replace them with newly detected notes. Continue?";

constexpr const char* kMouseSelectTool = "Mouse Select Tool";
constexpr const char* kDrawNoteTool = "Draw Note Tool";
constexpr const char* kLineAnchorTool = "Line Anchor Tool";
constexpr const char* kHandDrawTool = "Hand Draw Tool";
constexpr const char* kSplitNoteTool = "Split Note Tool";
constexpr const char* kVibratoTool = "Vibrato Tool";
constexpr const char* kUndoActionSplitClip = "Undo action Split Clip";
constexpr const char* kUndoActionMergeClips = "Undo action Merge Clips";
constexpr const char* kUndoActionDeleteClip = "Undo action Delete Clip";
constexpr const char* kUndoActionImportAudio = "Undo action Import Audio";
constexpr const char* kUndoActionMoveClip = "Undo action Move Clip";
constexpr const char* kUndoActionMoveClipToTrack = "Undo action Move Clip To Track";
constexpr const char* kUndoActionChangeClipGain = "Undo action Change Clip Gain";
constexpr const char* kUndoActionEditNotes = "Undo action Edit Notes";
constexpr const char* kUndoActionEditPitchCurve = "Undo action Edit Pitch Curve";
constexpr const char* kUndoActionEditAnchor = "Undo action Edit Anchor";
constexpr const char* kUndoActionDrawPitchCurve = "Undo action Draw Pitch Curve";
constexpr const char* kUndoActionDrawNote = "Undo action Draw Note";
constexpr const char* kUndoActionSplitNote = "Undo action Split Note";
constexpr const char* kUndoActionResizeNote = "Undo action Resize Note";
constexpr const char* kUndoActionMoveNotes = "Undo action Move Notes";
constexpr const char* kUndoActionDeleteNotes = "Undo action Delete Notes";
constexpr const char* kUndoActionToggleTrackMute = "Undo action Toggle Track Mute";
constexpr const char* kUndoActionToggleTrackSolo = "Undo action Toggle Track Solo";
constexpr const char* kUndoActionChangeTrackVolume = "Undo action Change Track Volume";
constexpr const char* kUndoActionScaleAutoTune = "Undo action Scale + Auto Tune";
constexpr const char* kUndoActionChangeScaleKey = "Undo action Change Scale/Key";
constexpr const char* kUndoActionChangeClipScaleKey = "Undo action Change Clip Scale/Key";
constexpr const char* kUndoActionInsertTrack = "Undo action Insert Track";
constexpr const char* kUndoActionDeleteTrack = "Undo action Delete Track";

constexpr const char* kPianoRollHintSelect1 = "PR hint: select click";
constexpr const char* kPianoRollHintSelect2 = "PR hint: select modifiers";
constexpr const char* kPianoRollHintSelect3 = "PR hint: select resize";
constexpr const char* kPianoRollHintSelect4 = "PR hint: select key";
constexpr const char* kPianoRollHintDrawNote1 = "PR hint: draw click drag";
constexpr const char* kPianoRollHintDrawNote2 = "PR hint: draw existing";
constexpr const char* kPianoRollHintDrawNote3 = "PR hint: draw key";
constexpr const char* kPianoRollHintAnchor1 = "PR hint: anchor place";
constexpr const char* kPianoRollHintAnchor2 = "PR hint: anchor drag";
constexpr const char* kPianoRollHintAnchor3 = "PR hint: anchor merge";
constexpr const char* kPianoRollHintAnchor4 = "PR hint: anchor esc";
constexpr const char* kPianoRollHintAnchor5 = "PR hint: anchor key";
constexpr const char* kPianoRollHintHandDraw1 = "PR hint: hand drag";
constexpr const char* kPianoRollHintHandDraw2 = "PR hint: hand note bound";
constexpr const char* kPianoRollHintHandDraw3 = "PR hint: hand key";
constexpr const char* kPianoRollHintVibrato1 = "PR hint: vibrato edges";
constexpr const char* kPianoRollHintVibrato2 = "PR hint: vibrato drag";
constexpr const char* kPianoRollHintVibrato3 = "PR hint: vibrato key";
constexpr const char* kPianoRollHintSplit1 = "PR hint: split click";
constexpr const char* kPianoRollHintSplit2 = "PR hint: split key";
constexpr const char* kPianoRollHintAuto1 = "PR hint: auto click";
constexpr const char* kPianoRollHintAuto2 = "PR hint: auto key";

}

inline juce::String get(Language lang, const char* key)
{
    static const struct Entry {
        const char* key;
        const char* en;
        const char* zh;
        const char* ja;
        const char* ru;
        const char* es;
    } translations[] = {
        { Keys::kFile, "File", "文件", "ファイル", "Файл", "Archivo" },
        { Keys::kEdit, "Edit", "编辑", "編集", "Правка", "Editar" },
        { Keys::kView, "View", "视图", "表示", "Вид", "Ver" },
        
        { Keys::kImportAudio, "Import Audio...", "导入音频...", "オーディオをインポート...", "Импорт аудио...", "Importar audio..." },
        { Keys::kExportAudio, "Export Audio", "导出音频", "オーディオをエクスポート", "Экспорт аудио", "Exportar audio" },
        { Keys::kExportSelectedClip, "Export Selected Clip", "导出选中的片段", "選択したクリップをエクスポート", "Экспорт клипа", "Exportar clip seleccionado" },
        { Keys::kExportTrack, "Export Track", "导出轨道", "トラックをエクスポート", "Экспорт дорожки", "Exportar pista" },
        { Keys::kExportBus, "Export Bus (Master Mix)", "导出总线混音", "バス（マスターミックス）をエクスポート", "Экспорт шины", "Exportar bus (mezcla maestra)" },
        { Keys::kExportStems, "Export Stems...", "分轨导出...", "ステムをエクスポート...", "Экспорт дорожек...", "Exportar pistas..." },
        { Keys::kExportStemsTitle, "Export Stems", "分轨导出", "ステムをエクスポート", "Экспорт дорожек", "Exportar pistas" },
        { Keys::kExportStemsPrefixLabel, "File name prefix:", "文件前缀：", "ファイルの接頭辞:", "Префикс:", "Prefijo:" },
        { Keys::kExportStemsTracksLabel, "Tracks:", "音轨：", "トラック:", "Дорожки:", "Pistas:" },
        { Keys::kExportStemsChooseFolder, "Select folder for stem export", "选择分轨导出目标文件夹", "エクスポート先フォルダを選択", "Папка для экспорта", "Carpeta de exportación" },
        { Keys::kExportStemsPickTrackWarning, "Please select at least one track that contains audio.", "请至少选择一条包含音频的音轨。", "オーディオのあるトラックを1つ以上選んでください。", "Выберите дорожку с аудио.", "Seleccione al menos una pista con audio." },
        { Keys::kExportStemsCompleteTitle, "Export Complete", "导出完成", "エクスポート完了", "Готово", "Exportación completada" },
        { Keys::kExportStemsCompleteMessage, "Exported {0} file(s) to:\n{1}", "已导出 {0} 个文件到：\n{1}", "{0} ファイルを次にエクスポートしました:\n{1}", "Экспортировано файлов: {0}\n{1}", "Se exportaron {0} archivo(s) a:\n{1}" },
        { Keys::kExportStemsFailedTitle, "Stem export failed", "分轨导出失败", "ステムのエクスポートに失敗", "Ошибка экспорта", "Error al exportar pistas" },
        { Keys::kExportAudioCompleteMessage, "{0}\nExported to:\n{1}", "{0}\n已导出到：\n{1}", "{0}\n書き出し先:\n{1}", "{0}\nЭкспорт в:\n{1}", "{0}\nExportado a:\n{1}" },
        { Keys::kExportAudioFailedTitle, "Export Failed", "导出失败", "エクスポート失敗", "Ошибка экспорта", "Error al exportar" },
        { Keys::kExportAudioFailedMessage, "Unable to export audio to:\n{0}", "无法导出音频到：\n{0}", "オーディオを書き出せません:\n{0}", "Не удалось экспортировать в:\n{0}", "No se pudo exportar audio a:\n{0}" },
        { Keys::kExportAudioFailedReason, "\nReason: {0}", "\n原因：{0}", "\n理由: {0}", "\nПричина: {0}", "\nMotivo: {0}" },
        { Keys::kOk, "OK", "确定", "OK", "OK", "Aceptar" },
        { Keys::kSaveProject, "Save Project...", "保存工程...", "プロジェクトを保存...", "Сохранить проект...", "Guardar proyecto..." },
        { Keys::kSaveProjectAs, "Save Project As...", "另存工程...", "名前を付けて保存...", "Сохранить как...", "Guardar proyecto como..." },
        { Keys::kLoadProject, "Open Project...", "打开工程...", "プロジェクトを開く...", "Открыть проект...", "Abrir proyecto..." },
        { Keys::kNewProject, "New Project", "新建工程", "新規プロジェクト", "Новый проект", "Proyecto nuevo" },
        { Keys::kUnsavedChangesTitle, "Unsaved changes", "未保存的更改", "未保存の変更", "Несохранённые изменения", "Cambios sin guardar" },
        { Keys::kUnsavedChangesMessage, "Save changes to the current project before creating a new one?", "新建工程前是否保存当前工程？", "新しいプロジェクトを作成する前に保存しますか？", "Сохранить текущий проект перед созданием нового?", "¿Guardar el proyecto actual antes de crear uno nuevo?" },
        { Keys::kUnsavedChangesLoadMessage, "Save changes to the current project before opening another one?", "打开其他工程前是否保存当前工程？", "別のプロジェクトを開く前に保存しますか？", "Сохранить текущий проект перед открытием другого?", "¿Guardar el proyecto actual antes de abrir otro?" },
        { Keys::kSave, "Save", "保存", "保存", "Сохранить", "Guardar" },
        { Keys::kDontSave, "Don't Save", "不保存", "保存しない", "Не сохранять", "No guardar" },
        { Keys::kProjectSavedTitle, "Project Saved", "工程已保存", "保存しました", "Сохранено", "Proyecto guardado" },
        { Keys::kProjectSavedMessage, "Project saved to:\n{0}", "工程已保存到：\n{0}", "保存先:\n{0}", "Сохранено:\n{0}", "Guardado en:\n{0}" },
        { Keys::kProjectSaveFailedTitle, "Save Failed", "保存失败", "保存に失敗", "Ошибка", "Error al guardar" },
        { Keys::kProjectSaveFailedMessage, "Failed to save project.", "无法保存工程。", "保存に失敗しました。", "Не удалось сохранить.", "No se pudo guardar." },
        { Keys::kProjectLoadedTitle, "Project Loaded", "工程已加载", "読み込みました", "Загружено", "Proyecto cargado" },
        { Keys::kProjectLoadedMessage, "Project loaded successfully.", "工程加载成功。", "読み込み成功。", "Готово.", "Carga correcta." },
        { Keys::kProjectLoadFailedTitle, "Load Failed", "加载失败", "読み込み失敗", "Ошибка", "Error al abrir" },
        { Keys::kProjectLoadFailedMessage, "Failed to load project or invalid file.", "无法加载工程或文件无效。", "読み込めません。", "Неверный файл.", "Archivo inválido." },
        { Keys::kRecentProjects, "Recent Projects", "最近工程", "最近のプロジェクト", "Недавние проекты", "Recientes" },
        { Keys::kRecentProjectsEmpty, "No recent projects", "暂无最近工程", "履歴なし", "Нет проектов", "Sin recientes" },
        { Keys::kOptions, "Options", "选项", "オプション", "Настройки", "Opciones" },
        { Keys::kStandaloneAudioOutputSampleRate, "Output sample rate", "输出采样率", "出力サンプルレート", "Частота дискретизации", "Frecuencia de muestreo" },
        { Keys::kStandaloneAudioSampleRateDriverDefault, "Driver default", "驱动默认", "ドライバ既定", "По умолчанию", "Predeterminado del controlador" },
        
        { Keys::kUndo, "Undo", "撤销", "元に戻す", "Отменить", "Deshacer" },
        { Keys::kRedo, "Redo", "重做", "やり直す", "Повтор", "Rehacer" },
        { Keys::kUndoToMenuTitle, "Undo to", "撤回到", "ここまで元に戻す", "Отменить до", "Deshacer hasta" },
        { Keys::kUndoToMenuEmpty, "No actions available", "没有可撤销的操作", "操作履歴がありません", "Нет доступных действий", "No hay acciones disponibles" },
        
        { Keys::kShowWaveform, "Show Waveform", "显示波形", "波形を表示", "Волновая форма", "Ver forma de onda" },
        { Keys::kShowLanes, "Show Lanes", "显示音道", "レーンを表示", "Дорожки", "Ver carriles" },
        { Keys::kNoteNames, "Note Names", "音名", "ノート名", "Названия нот", "Nombres de notas" },
        { Keys::kShowAllNotes, "Show All Notes", "显示全部音名", "すべてのノート名を表示", "Все ноты", "Mostrar todas" },
        { Keys::kShowCOnly, "Show C Only", "仅显示C", "Cのみ表示", "Только C", "Solo C" },
        { Keys::kHideNoteNames, "Hide Note Names", "不显示音名", "ノート名を非表示", "Скрыть", "Ocultar nombres" },
        { Keys::kShowNoteBlockNoteNames, "Note Names on Blocks", "音符音名", "ブロック上の音名", "Названия на нотах", "Nombres en notas" },
        { Keys::kTheme, "Theme", "主题", "テーマ", "Тема", "Tema" },
        { Keys::kThemeBlueBreeze, "Blue Breeze", "蓝色清风", "ブルーブリーズ", "Голубой бриз", "Brisa azul" },
        { Keys::kThemeDarkBlueGrey, "Dark Blue-Grey", "深蓝灰", "ダークブルーグレー", "Тёмно-синий серый", "Azul-gris oscuro" },
        { Keys::kThemeAurora, "Aurora Glass", "极光玻璃", "オーロラグラス", "Аврора", "Aurora cristal" },
        { Keys::kMouseTrail, "Mouse Trail", "鼠标轨迹", "マウストレイル", "След мыши", "Rastro del ratón" },
        { Keys::kOff, "Off", "关闭", "オフ", "Выкл.", "Apagado" },
        { Keys::kClassic, "Classic", "经典", "クラシック", "Классика", "Clásico" },
        { Keys::kNeon, "Neon", "霓虹", "ネオン", "Неон", "Neón" },
        { Keys::kFire, "Fire", "火焰", "ファイア", "Огонь", "Fuego" },
        { Keys::kOcean, "Ocean", "海洋", "オーシャン", "Океан", "Océano" },
        { Keys::kGalaxy, "Galaxy", "银河", "ギャラクシー", "Галактика", "Galaxia" },
        { Keys::kCherryBlossom, "Cherry Blossom", "樱花", "桜", "Сакура", "Cerezo" },
        { Keys::kMatrix, "Matrix", "矩阵", "マトリックス", "Матрица", "Matrix" },
        
        { Keys::kAudio, "Audio", "音频", "オーディオ", "Аудио", "Audio" },
        { Keys::kMouse, "Mouse", "鼠标", "マウス", "Мышь", "Ratón" },
        { Keys::kKeyswitch, "Keyswitch", "快捷键", "キースイッチ", "Клавиши", "Atajos" },
        { Keys::kLanguage, "Language", "语言", "言語", "Язык", "Idioma" },
        { Keys::kLanguageLabel, "Interface Language", "界面语言", "インターフェース言語", "Язык", "Idioma" },
        
        { Keys::kHorizontalZoomSensitivity, "Horizontal Zoom Sensitivity", "水平缩放灵敏度", "水平ズーム感度", "Чувств. гориз. zoom", "Sensibilidad zoom horizontal" },
        { Keys::kVerticalZoomSensitivity, "Vertical Zoom Sensitivity", "垂直缩放灵敏度", "垂直ズーム感度", "Чувств. верт. zoom", "Sensibilidad zoom vertical" },
        { Keys::kScrollSpeed, "Scroll Speed", "滚动速度", "スクロール速度", "Скорость прокрутки", "Velocidad" },
        { Keys::kResetToDefaults, "Reset to Defaults", "恢复默认设置", "デフォルトに戻す", "Сбросить", "Restablecer" },
        
        { Keys::kSetShortcut, "Set Shortcut", "设置快捷键", "ショートカットを設定", "Назначить сочетание", "Atajo" },
        { Keys::kPressNewKeyCombination, "Press the new key combination", "按下新的组合键", "新しいキーの組み合わせを押してください", "Нажмите сочетание", "Pulse combinación" },
        { Keys::kCurrent, "Current", "当前", "現在", "Текущий", "Actual" },
        { Keys::kCancel, "Cancel", "取消", "キャンセル", "Отмена", "Cancelar" },
        { Keys::kShortcutConflict, "Shortcut Conflict", "快捷键冲突", "ショートカットの競合", "Конфликт сочетаний", "Conflicto de atajo" },
        { Keys::kShortcutConflictMessage, "This shortcut is already assigned to \"{0}\".\n\nDo you want to reassign it?", "此快捷键已分配给\"{0}\"。\n\n是否重新分配？", "このショートカットは既に「{0}」に割り当てられています。\n\n再割り当てしますか？", "Это сочетание уже назначено для \"{0}\".\n\nПереназначить?", "Este atajo ya está asignado a \"{0}\".\n\n¿Reasignar?" },
        { Keys::kYes, "Yes", "是", "はい", "Да", "Sí" },
        { Keys::kNo, "No", "否", "いいえ", "Нет", "No" },
        { Keys::kResetAllToDefaults, "Reset All to Defaults", "全部恢复默认", "すべてデフォルトに戻す", "Сбросить все", "Restablecer todo" },
        
        { Keys::kMenuBarTitleFile, "File (F)", "文件 (F)", "ファイル (F)", "Файл (F)", "Archivo (F)" },
        { Keys::kMenuBarTitleEdit, "Edit (E)", "编辑 (E)", "編集 (E)", "Правка (E)", "Editar (E)" },
        { Keys::kMenuBarTitleView, "View (V)", "视图 (V)", "表示 (V)", "Вид (V)", "Ver (V)" },
        { Keys::kMenuBarTitleHelp, "Help (H)", "帮助 (H)", "ヘルプ (H)", "Справка (H)", "Ayuda (H)" },
        { Keys::kOpenSourceRepository, "Source repository", "开源仓库", "ソースリポジトリ", "Репозиторий исходников", "Repositorio de código" },
        { Keys::kShortcutGroupTransport, "Transport", "走带", "トランスポート", "Транспорт", "Transporte" },
        { Keys::kShortcutGroupProject, "Project", "工程", "プロジェクト", "Проект", "Proyecto" },
        { Keys::kShortcutGroupExport, "Export", "导出", "書き出し", "Экспорт", "Exportar" },
        { Keys::kShortcutGroupEdit, "Edit", "编辑", "編集", "Правка", "Edición" },
        { Keys::kShortcutGroupPianoRoll, "Piano roll & arrangement", "钢琴卷帘与编曲区", "ピアノロールとアレンジ", "Пиано-ролл и аранжировка", "Piano roll y arreglo" },
        { Keys::kShortcutGroupView, "View & options", "视图与选项", "表示とオプション", "Вид и параметры", "Vista y opciones" },
        { Keys::kShortcutGroupHelp, "Help", "帮助", "ヘルプ", "Справка", "Ayuda" },
        
        { Keys::kPlayPause, "Play/Pause", "播放/暂停", "再生/一時停止", "Старт/Пауза", "Play/Pausa" },
        { Keys::kStop, "Stop", "停止", "停止", "Стоп", "Detener" },
        { Keys::kPlayFromStart, "Play from Start", "从头播放", "最初から再生", "Играть сначала", "Reprod. inicio" },
        { Keys::kCut, "Cut", "剪切", "切り取り", "Вырезать", "Cortar" },
        { Keys::kCopy, "Copy", "复制", "コピー", "Копия", "Copiar" },
        { Keys::kPaste, "Paste", "粘贴", "貼り付け", "Вставить", "Pegar" },
        { Keys::kSelectAll, "Select All", "全选", "すべて選択", "Выбрать всё", "Selec. todo" },
        { Keys::kDelete, "Delete", "删除", "削除", "Удалить", "Eliminar" },
        
        { Keys::kPitchCorrection, "Pitch correction", "音高校正", "ピッチ補正", "Коррекция тона", "Corrección de tono" },
        { Keys::kRetuneSpeed, "Retune Speed", "校正速度", "チューン速度", "Скорость коррекции", "Vel. afinación" },
        { Keys::kVibratoDepth, "Vib. Depth", "颤音深度", "ビブラート深さ", "Глуб. вибрато", "Prof. vibrato" },
        { Keys::kVibratoRate, "Vib. Rate", "颤音速率", "ビブラート速度", "Скор. вибрато", "Tasa vibrato" },
        { Keys::kNoteSplit, "Note Split", "音符分割", "ノート分割", "Разд. нот", "Div. notas" },
        { Keys::kTools, "Tools", "工具", "ツール", "Инструменты", "Herram." },
        { Keys::kAuto, "Auto", "自动", "オート", "Авто", "Auto" },
        { Keys::kSelect, "Select", "选择", "選択", "Выбор", "Selec." },
        { Keys::kDrawNotes, "Draw notes", "绘制音符", "ノートを描画", "Рисовать ноты", "Dib. notas" },
        { Keys::kLineAnchor, "Line anchor", "锚点", "ラインアンカー", "Якорь", "Ancla línea" },
        { Keys::kHandDraw, "Hand draw pitch", "手绘音高", "手描きピッチ", "Рисование высоты", "Dib. tono" },
        { Keys::kSplitNote, "Split note", "分割音符", "ノート分割", "Разделить ноту", "Dividir nota" },
        { Keys::kAutoOptionsTitle, "Auto Pitch Detection Options", "自动音高检测选项", "自動ピッチ検出オプション", "Параметры автокоррекции высоты", "Opciones de detección automática de tono" },
        { Keys::kAutoOverwriteWarning, "This clip already contains edited content.\nRe-running AUTO will discard all current edits. Continue?", "当前 Clip 中存在已修改内容。\n重新执行 AUTO 将丢失所有当前修改，是否继续？", "このクリップには既存の編集があります。\nAUTO を再実行すると現在の編集内容は失われます。続行しますか？", "В этом клипе уже есть правки.\nПовторный запуск AUTO удалит текущие изменения. Продолжить?", "Este clip ya contiene ediciones.\nVolver a ejecutar AUTO descartará los cambios actuales. ¿Continuar?" },
        { Keys::kAutoUseAsDefaultAndSkip, "Use these settings as default and run directly next time (right-click AUTO to reopen options)", "此后沿用当前参数为默认并直接渲染（右键点击 AUTO 可重新打开参数对话框）", "この設定を既定にして次回から直接実行（AUTO を右クリックでオプション再表示）", "Использовать эти параметры по умолчанию и запускать сразу (ПКМ по AUTO — открыть параметры)", "Usar estos ajustes por defecto y ejecutar directamente (clic derecho en AUTO para abrir opciones)" },
        { Keys::kAutoRightClickOptions, "Right-click for options", "右键打开参数", "右クリックでオプション", "ПКМ: параметры", "Clic derecho: opciones" },
        
        { Keys::kPlay, "Play", "播放", "再生", "Старт", "Reprod." },
        { Keys::kPause, "Pause", "暂停", "一時停止", "Пауза", "Pausar" },
        { Keys::kLoop, "Loop", "循环", "ループ", "Цикл", "Bucle" },
        { Keys::kTapTempo, "Tap Tempo", "敲击节拍", "タップテンポ", "Тап темп", "Tap tempo" },
        { Keys::kTransportBypass, "Bypass pitch correction", "旁路音高校正（干声）", "ピッチ補正をバイパス（ドライ）", "Обход коррекции тона (сухой)", "Omitir corrección de tono (seco)" },
        { Keys::kTransportTimeDisplay, "Position; click to switch time / bars", "播放位置；点击切换时间码/小节", "再生位置；クリックで時間/小節表示切替", "Позиция; клик — время/такт", "Posición; clic para tiempo/compases" },
        { Keys::kTransportBpm, "Tempo (BPM)", "速度（BPM）", "テンポ（BPM）", "Темп (BPM)", "Tempo (BPM)" },
        { Keys::kTransportScaleRoot, "Key (root note)", "调性根音", "キー（ルート）", "Тональность (основная)", "Tonalidad (fundamental)" },
        { Keys::kTransportScaleType, "Scale type", "音阶类型", "スケール種類", "Тип гаммы", "Tipo de escala" },
        { Keys::kTrackView, "Track View", "轨道视图", "トラックビュー", "Вид дорожки", "Vista pista" },
        { Keys::kPianoRollView, "Piano Roll View", "钢琴卷帘视图", "ピアノロールビュー", "Вид пиано-ролла", "Vista piano" },
        
        { Keys::kTracks, "Tracks", "轨道", "トラック", "Дорожки", "Pistas" },
        { Keys::kProps, "Props", "属性", "プロパティ", "Свойства", "Props" },
        { Keys::kScale, "Scale", "调式", "スケール", "Гамма", "Escala" },
        
        { Keys::kClose, "Close", "关闭", "閉じる", "Закрыть", "Cerrar" },
        { Keys::kShortcutSplitClip, "Split clip (Arrangement)", "切分片段（编曲区）", "クリップ分割（アレンジ）", "Разделить клип (аранжировка)", "Dividir clip (arreglo)" },
        { Keys::kArrangementSplitAtPlayhead, "Split at playhead", "在播放头处切分", "再生ヘッドで分割", "Разделить по позиции воспроизведения", "Dividir en cabezal" },
        { Keys::kArrangementMergeWithNextClip, "Merge with next clip", "与下一段合并", "次のクリップと結合", "Объединить со следующим клипом", "Fusionar con el siguiente clip" },
        { Keys::kArrangementRenameClip, "Rename clip...", "重命名片段...", "クリップ名を変更...", "Переименовать клип...", "Renombrar clip..." },
        { Keys::kTrackInsert, "Insert Track", "插入轨道", "トラックを挿入", "Вставить дорожку", "Insertar pista" },
        { Keys::kTrackDelete, "Delete Track", "删除轨道", "トラックを削除", "Удалить дорожку", "Eliminar pista" },
        { Keys::kRenameClipDialogTitle, "Rename clip", "重命名片段", "クリップ名の変更", "Переименование клипа", "Renombrar clip" },
        { Keys::kRenameClipPrompt, "Enter a new name for this clip:", "请输入新的片段名称：", "新しいクリップ名を入力:", "Введите новое имя клипа:", "Escriba un nombre nuevo para el clip:" },
        { Keys::kRenameClipEditorLabel, "Name", "名称", "名前", "Имя", "Nombre" },
        { Keys::kToolbarTooltipShortcut, "Shortcut:", "快捷键:", "ショートカット:", "Сочетание:", "Atajo:" },
        { Keys::kAutoTuneNotesPresentTitle, "AUTO", "AUTO", "AUTO", "AUTO", "AUTO" },
        { Keys::kAutoTuneNotesPresentMessage,
          "This clip already has notes. AUTO will replace them with newly detected notes. Continue?",
          "当前片段已有音符。执行 AUTO 将用新检测的音符替换现有音符。是否继续？",
          "このクリップには既にノートがあります。AUTO は新しく検出したノートで置き換えます。続行しますか？",
          "В клипе уже есть ноты. AUTO заменит их вновь обнаруженными. Продолжить?",
          "Este clip ya tiene notas. AUTO las sustituirá por las recién detectadas. ¿Continuar?" },
        { Keys::kHelp, "Help...", "帮助...", "ヘルプ...", "Справка...", "Ayuda..." },
        
        { Keys::kMouseSelectTool, "Mouse Select Tool", "鼠标选择工具", "マウス選択ツール", "Инструмент выбора", "Herram. selec." },
        { Keys::kDrawNoteTool, "Draw Note Tool", "绘制音符工具", "ノート描画ツール", "Рисование нот", "Herram. dibujo" },
        { Keys::kLineAnchorTool, "Line Anchor Tool", "锚点工具", "ラインアンカーツール", "Инструмент якоря", "Herram. ancla" },
        { Keys::kHandDrawTool, "Hand Draw Tool", "手绘工具", "手描きツール", "Рисование", "Herram. libre" },
        { Keys::kSplitNoteTool, "Split Note Tool", "分割音符工具", "ノート分割ツール", "Инструмент разделения", "Herram. dividir" },
        { Keys::kVibratoTool, "Vibrato Tool", "颤音工具", "ビブラートツール", "Вибрато", "Herram. vibrato" },
        { Keys::kUndoActionSplitClip, "Split Clip", "切分片段", "クリップを分割", "Разделить клип", "Dividir clip" },
        { Keys::kUndoActionMergeClips, "Merge Clips", "合并片段", "クリップを結合", "Объединить клипы", "Unir clips" },
        { Keys::kUndoActionDeleteClip, "Delete Clip", "删除片段", "クリップを削除", "Удалить клип", "Eliminar clip" },
        { Keys::kUndoActionImportAudio, "Import Audio", "导入音频", "オーディオをインポート", "Импорт аудио", "Importar audio" },
        { Keys::kUndoActionMoveClip, "Move Clip", "移动片段", "クリップを移動", "Переместить клип", "Mover clip" },
        { Keys::kUndoActionMoveClipToTrack, "Move Clip to Track", "跨轨移动片段", "クリップを別トラックへ移動", "Переместить клип на дорожку", "Mover clip a pista" },
        { Keys::kUndoActionChangeClipGain, "Change Clip Gain", "调整片段增益", "クリップゲインを変更", "Изменить усиление клипа", "Cambiar ganancia del clip" },
        { Keys::kUndoActionEditNotes, "Edit Notes", "编辑音符", "ノートを編集", "Редактировать ноты", "Editar notas" },
        { Keys::kUndoActionEditPitchCurve, "Edit Pitch Curve", "编辑音高曲线", "ピッチカーブを編集", "Редактировать кривую высоты", "Editar curva de tono" },
        { Keys::kUndoActionEditAnchor, "Edit Anchor", "编辑锚点", "アンカーを編集", "Редактировать якоря", "Editar anclas" },
        { Keys::kUndoActionDrawPitchCurve, "Draw F0 Curve", "绘制音高曲线", "F0カーブを描画", "Рисовать кривую F0", "Dibujar curva F0" },
        { Keys::kUndoActionDrawNote, "Draw Note", "绘制音符", "ノートを描画", "Нарисовать ноту", "Dibujar nota" },
        { Keys::kUndoActionSplitNote, "Split Note", "切分音符", "ノートを分割", "Разделить ноту", "Dividir nota" },
        { Keys::kUndoActionResizeNote, "Resize Note", "调整音符长度", "ノート長を変更", "Изменить длительность ноты", "Redimensionar nota" },
        { Keys::kUndoActionMoveNotes, "Move Notes", "移动音符", "ノートを移動", "Переместить ноты", "Mover notas" },
        { Keys::kUndoActionDeleteNotes, "Delete Notes", "删除音符", "ノートを削除", "Удалить ноты", "Eliminar notas" },
        { Keys::kUndoActionToggleTrackMute, "Toggle Track Mute", "切换轨道静音", "トラックミュート切替", "Переключить mute дорожки", "Alternar silencio de pista" },
        { Keys::kUndoActionToggleTrackSolo, "Toggle Track Solo", "切换轨道独奏", "トラックソロ切替", "Переключить solo дорожки", "Alternar solo de pista" },
        { Keys::kUndoActionChangeTrackVolume, "Change Track Volume", "调整轨道音量", "トラック音量を変更", "Изменить громкость дорожки", "Cambiar volumen de pista" },
        { Keys::kUndoActionScaleAutoTune, "Scale + Auto Tune", "调式 + 自动校正", "スケール + 自動補正", "Лад + авто-тюн", "Escala + Auto Tune" },
        { Keys::kUndoActionChangeScaleKey, "Change Scale/Key", "更改调式/调性", "スケール/キーを変更", "Изменить лад/тональность", "Cambiar escala/tono" },
        { Keys::kUndoActionChangeClipScaleKey, "Change Clip Scale/Key", "更改片段调式/调性", "クリップのスケール/キーを変更", "Изменить лад/тональность клипа", "Cambiar escala/tono del clip" },
        { Keys::kUndoActionInsertTrack, "Insert Track", "插入轨道", "トラックを挿入", "Вставить дорожку", "Insertar pista" },
        { Keys::kUndoActionDeleteTrack, "Delete Track", "删除轨道", "トラックを削除", "Удалить дорожку", "Eliminar pista" },

        { Keys::kPianoRollHintSelect1, "Click a note to select; drag on empty area to box-select.", "单击音符可选中；在空白处拖拽可框选。", "ノートをクリックで選択；空き領域をドラッグで範囲選択。", "Клик по ноте — выбор; рамка на пустом месте.", "Clic en nota para seleccionar; arrastra en vacío para marco." },
        { Keys::kPianoRollHintSelect2, "Ctrl/Cmd+click toggles; Shift+click extends selection between notes.", "Ctrl/Cmd+单击反选；Shift+单击在音符间扩展选区。", "Ctrl/Cmd+クリックで切替；Shift+クリックで範囲拡張。", "Ctrl/Cmd — переключить; Shift — диапазон.", "Ctrl/Cmd alterna; Shift extiende la selección." },
        { Keys::kPianoRollHintSelect3, "Drag note left/right edges to resize; drag selected notes to move.", "拖拽音符左右边缘调整时长；拖拽已选音符可移动。", "左右端をドラッグで長さ変更；選択ノートをドラッグで移動。", "Тяните края ноты; перетаскивайте выбранные.", "Arrastra bordes para redimensionar; mueve notas seleccionadas." },
        { Keys::kPianoRollHintSelect4, "Shortcut [1] Select tool.", "快捷键 [1] 选择工具。", "ショートカット [1] 選択ツール。", "Клавиша [1] — выбор.", "Atajo [1] herramienta Seleccionar." },
        { Keys::kPianoRollHintDrawNote1, "Click empty area, then drag horizontally to draw a new note.", "在空白处按下并水平拖拽以绘制新音符。", "空きをクリックし横にドラッグで新規ノート。", "Клик по пустому месту и тяните вдоль времени.", "Clic en vacío y arrastra en horizontal para dibujar." },
        { Keys::kPianoRollHintDrawNote2, "Click an existing note to select (Ctrl toggles multi-select).", "单击已有音符可选中（Ctrl 切换多选）。", "既存ノートをクリックで選択（Ctrlで複数）。", "Клик по ноте — выбор (Ctrl — несколько).", "Clic en nota existente para seleccionar (Ctrl multi)." },
        { Keys::kPianoRollHintDrawNote3, "Shortcut [2] Draw Note tool.", "快捷键 [2] 绘制音符工具。", "[2] ノート描画ツール。", "[2] — рисование нот.", "Atajo [2] dibujar nota." },
        { Keys::kPianoRollHintAnchor1, "Left-click to add anchors; the line is fitted through them.", "左键单击添加锚点；系统将拟合穿过锚点的音高线。", "左クリックでアンカー追加；滑らかな線をフィット。", "ЛКМ — якоря; кривая строится по ним.", "Clic izq. añade anclas; se ajusta la curva." },
        { Keys::kPianoRollHintAnchor2, "Drag anchors to adjust; Ctrl+drag draws a box to select anchors.", "拖拽锚点调整；Ctrl+拖拽框选锚点。", "アンカーをドラッグ；Ctrl+ドラッグで範囲選択。", "Тяните якоря; Ctrl+рамка — выбор якорей.", "Arrastra anclas; Ctrl+arrastre caja de selección." },
        { Keys::kPianoRollHintAnchor3, "Shift+click adds to selection; on same note, chains can merge.", "Shift+单击加入选区；同音符内可合并锚点链。", "Shift+クリックで追加選択；同一ノート内で結合可。", "Shift — добавить; на одной ноте цепочки сливаются.", "Shift añade a selección; fusiona cadenas en la misma nota." },
        { Keys::kPianoRollHintAnchor4, "Esc cancels in-progress placement when applicable.", "适用时 Esc 可取消正在放置的锚点。", "配置中は Esc でキャンセル可。", "Esc — отмена размещения.", "Esc cancela la colocación en curso." },
        { Keys::kPianoRollHintAnchor5, "Shortcut [3] Line Anchor tool.", "快捷键 [3] 锚点工具。", "[3] ラインアンカー。", "[3] — якоря.", "Atajo [3] ancla de línea." },
        { Keys::kPianoRollHintHandDraw1, "Click-drag on the roll to hand-paint pitch (inside notes).", "在卷帘上按住拖拽手绘音高（限制在音符范围内）。", "ドラッグで手描きピッチ（ノート内のみ有効）。", "Рисуйте перетаскиванием (внутри нот).", "Arrastra para pintar tono (solo dentro de notas)." },
        { Keys::kPianoRollHintHandDraw2, "Strokes outside any note are discarded automatically.", "音符外的笔划会被自动丢弃。", "ノート外のストロークは破棄されます。", "Вне нот штрихи отбрасываются.", "Fuera de notas el trazo se descarta." },
        { Keys::kPianoRollHintHandDraw3, "Shortcut [4] Hand Draw tool.", "快捷键 [4] 手绘工具。", "[4] 手描きツール。", "[4] — ручное рисование.", "Atajo [4] dibujo libre." },
        { Keys::kPianoRollHintVibrato1, "Hover note left/right edges for rate; top/bottom for depth.", "音符左右边缘调整速率；上下边缘调整深度。", "左右端で速度、上下端で深さ。", "Края слева/справа — скорость; сверху/снизу — глубина.", "Bordes izq./der.: velocidad; arriba/abajo: profundidad." },
        { Keys::kPianoRollHintVibrato2, "Drag horizontally to change vibrato rate; vertically to change depth.", "水平拖动改变颤音速率，垂直拖动改变深度。", "横ドラッグで速度、縦で深さ。", "Горизонтально — скорость вибрато; вертикально — глубина.", "Arrastra en horizontal: velocidad; vertical: profundidad." },
        { Keys::kPianoRollHintVibrato3, "Shortcut [5] Vibrato tool.", "快捷键 [5] 颤音工具。", "[5] ビブラート。", "[5] — вибрато.", "Atajo [5] vibrato." },
        { Keys::kPianoRollHintSplit1, "Click a note at the mouse position to split it into two.", "在鼠标位置单击音符可将其一分为二。", "マウス位置でノートをクリックして分割。", "Клик по ноте под курсором — разрез.", "Clic en la nota bajo el cursor para partirla." },
        { Keys::kPianoRollHintSplit2, "Shortcut [6] Split Note tool.", "快捷键 [6] 分割音符工具。", "[6] ノート分割。", "[6] — разрезать.", "Atajo [6] dividir nota." },
        { Keys::kPianoRollHintAuto1, "Click to run AUTO for the full current clip using its Original F0.", "单击可基于当前 Clip 的整段 Original F0 执行 AUTO。", "クリックで現在のクリップ全体の Original F0 に対して AUTO を実行。", "Клик — запустить AUTO для всего текущего клипа по его Original F0.", "Clic para ejecutar AUTO sobre todo el clip actual usando su Original F0." },
        { Keys::kPianoRollHintAuto2, "Right-click AUTO for options; default shortcut Ctrl/Cmd+Shift+7 (change in Options → Keyswitch).", "右键 AUTO 打开参数；默认快捷键 Ctrl/Cmd+Shift+7（可在选项→快捷键中修改）。", "AUTO を右クリックでオプション。既定のショートカットは Ctrl/Cmd+Shift+7（オプション→キースイッチで変更可能）。", "ПКМ по AUTO — параметры; по умолчанию Ctrl/Cmd+Shift+7 (можно изменить в Настройки → клавиши).", "Clic derecho en AUTO para opciones; atajo por defecto Ctrl/Cmd+Shift+7 (puede cambiarse en Opciones → atajos)." },
    };
    
    for (const auto& t : translations)
    {
        if (std::strcmp(t.key, key) == 0)
        {
            switch (lang)
            {
                case Language::English:  return juce::String::fromUTF8(t.en);
                case Language::Chinese:  return juce::String::fromUTF8(t.zh);
                case Language::Japanese: return juce::String::fromUTF8(t.ja);
                case Language::Russian:  return juce::String::fromUTF8(t.ru);
                case Language::Spanish:  return juce::String::fromUTF8(t.es);
                default: return juce::String::fromUTF8(t.en);
            }
        }
    }
    
    return juce::String::fromUTF8(key);
}

inline juce::String get(const char* key)
{
    return get(LocalizationManager::getInstance().getLanguage(), key);
}

inline juce::String format(const juce::String& pattern, const juce::String& arg0)
{
    return pattern.replace("{0}", arg0);
}

inline juce::String format(const juce::String& pattern, const juce::String& arg0, const juce::String& arg1)
{
    return pattern.replace("{0}", arg0).replace("{1}", arg1);
}

}

#define LOC(key) OpenTune::Loc::get(OpenTune::Loc::Keys::key)
#define LOC_KEY(key) OpenTune::Loc::get(key)
#define LOC_RAW(key) OpenTune::Loc::get(OpenTune::LocalizationManager::getInstance().getLanguage(), key)

}
