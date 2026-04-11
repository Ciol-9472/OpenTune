#pragma once

#include <juce_core/juce_core.h>
#include <array>
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
constexpr const char* kOk = "OK";
constexpr const char* kSaveProject = "Save Project...";
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

constexpr const char* kUndo = "Undo";
constexpr const char* kRedo = "Redo";

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

constexpr const char* kPlay = "Play";
constexpr const char* kPause = "Pause";
constexpr const char* kLoop = "Loop";
constexpr const char* kTapTempo = "Tap Tempo";
constexpr const char* kTrackView = "Track View";
constexpr const char* kPianoRollView = "Piano Roll View";

constexpr const char* kTracks = "Tracks";
constexpr const char* kProps = "Props";
constexpr const char* kScale = "Scale";

        constexpr const char* kClose = "Close";
        constexpr const char* kHelp = "Help...";

constexpr const char* kMouseSelectTool = "Mouse Select Tool";
constexpr const char* kDrawNoteTool = "Draw Note Tool";
constexpr const char* kLineAnchorTool = "Line Anchor Tool";
constexpr const char* kHandDrawTool = "Hand Draw Tool";
constexpr const char* kSplitNoteTool = "Split Note Tool";

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
        { Keys::kOk, "OK", "确定", "OK", "OK", "Aceptar" },
        { Keys::kSaveProject, "Save Project...", "保存工程...", "プロジェクトを保存...", "Сохранить проект...", "Guardar proyecto..." },
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
        
        { Keys::kUndo, "Undo", "撤销", "元に戻す", "Отменить", "Deshacer" },
        { Keys::kRedo, "Redo", "重做", "やり直す", "Повтор", "Rehacer" },
        
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
        
        { Keys::kPlay, "Play", "播放", "再生", "Старт", "Reprod." },
        { Keys::kPause, "Pause", "暂停", "一時停止", "Пауза", "Pausar" },
        { Keys::kLoop, "Loop", "循环", "ループ", "Цикл", "Bucle" },
        { Keys::kTapTempo, "Tap Tempo", "敲击节拍", "タップテンポ", "Тап темп", "Tap tempo" },
        { Keys::kTrackView, "Track View", "轨道视图", "トラックビュー", "Вид дорожки", "Vista pista" },
        { Keys::kPianoRollView, "Piano Roll View", "钢琴卷帘视图", "ピアノロールビュー", "Вид пиано-ролла", "Vista piano" },
        
        { Keys::kTracks, "Tracks", "轨道", "トラック", "Дорожки", "Pistas" },
        { Keys::kProps, "Props", "属性", "プロパティ", "Свойства", "Props" },
        { Keys::kScale, "Scale", "调式", "スケール", "Гамма", "Escala" },
        
        { Keys::kClose, "Close", "关闭", "閉じる", "Закрыть", "Cerrar" },
        { Keys::kHelp, "Help...", "帮助...", "ヘルプ...", "Справка...", "Ayuda..." },
        
        { Keys::kMouseSelectTool, "Mouse Select Tool", "鼠标选择工具", "マウス選択ツール", "Инструмент выбора", "Herram. selec." },
        { Keys::kDrawNoteTool, "Draw Note Tool", "绘制音符工具", "ノート描画ツール", "Рисование нот", "Herram. dibujo" },
        { Keys::kLineAnchorTool, "Line Anchor Tool", "锚点工具", "ラインアンカーツール", "Инструмент якоря", "Herram. ancla" },
        { Keys::kHandDrawTool, "Hand Draw Tool", "手绘工具", "手描きツール", "Рисование", "Herram. libre" },
        { Keys::kSplitNoteTool, "Split Note Tool", "分割音符工具", "ノート分割ツール", "Инструмент разделения", "Herram. dividir" },

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
        { Keys::kPianoRollHintSplit1, "Click a note at the mouse position to split it into two.", "在鼠标位置单击音符可将其一分为二。", "マウス位置でノートをクリックして分割。", "Клик по ноте под курсором — разрез.", "Clic en la nota bajo el cursor para partirla." },
        { Keys::kPianoRollHintSplit2, "Shortcut [5] Split Note tool.", "快捷键 [5] 分割音符工具。", "[5] ノート分割。", "[5] — разрезать.", "Atajo [5] dividir nota." },
        { Keys::kPianoRollHintAuto1, "Click to run Auto note generation for the current F0 range/selection.", "单击可根据当前 F0 与选区自动生成音符。", "クリックでF0範囲から自動ノート生成。", "Клик — авто-ноты по F0/выделению.", "Clic: generar notas automáticas según F0/selección." },
        { Keys::kPianoRollHintAuto2, "Shortcut [6] when the piano roll has keyboard focus.", "钢琴窗拥有键盘焦点时快捷键 [6]。", "ピアノロールにフォーカスがあるとき [6]。", "Фокус на ролле — клавиша [6].", "Con foco en el piano roll, atajo [6]." },
    };
    
    for (const auto& t : translations)
    {
        if (strcmp(t.key, key) == 0)
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
