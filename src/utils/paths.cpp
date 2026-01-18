#include "utils/paths.h"

#include "utils/logging.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

// Fix Python.h conflict with Qt's slots macro
#ifdef slots
    #undef slots
    #include <Python.h>
    #define slots Q_SLOTS
#else
    #include <Python.h>
#endif

static Logger logger = Logger::getLogger("Paths");

// Constants from Python version
const int MAX_FILENAME_LENGTH = 255;
const int MAX_FILEPATH_LENGTH = 4096;

QString Paths::slugify(const QString& text) {
    // Adapted from Django's slugify to match Python version behavior
    // https://docs.djangoproject.com/en/4.1/ref/utils/#django.utils.text.slugify

    QString result = text;

    // Normalize Unicode to NFKD and convert to ASCII
    // This decomposes characters (e.g., "é" -> "e" + combining accent)
    result = result.normalized(QString::NormalizationForm_KD);

    // Remove non-ASCII characters (keep only ASCII)
    QString ascii_only;
    for (const QChar& c : result) {
        if (c.unicode() < 128) {
            ascii_only += c;
        }
    }
    result = ascii_only;

    // Convert to lowercase
    result = result.toLower();

    // Remove characters that are not alphanumerics, underscores, spaces, or hyphens
    // Python's \w includes underscore, so we keep it: [^\w\s-] -> [^a-z0-9_\s-]
    result.replace(QRegularExpression("[^a-z0-9_\\s-]"), "");

    // Replace whitespace and multiple hyphens with single hyphen
    result.replace(QRegularExpression("[-\\s]+"), "-");

    // Strip leading/trailing hyphens and underscores
    while (result.startsWith('-') || result.startsWith('_')) {
        result = result.mid(1);
    }
    while (result.endsWith('-') || result.endsWith('_')) {
        result.chop(1);
    }

    // Python version doesn't have "unnamed" fallback, but we keep it for safety
    if (result.isEmpty()) {
        result = "unnamed";
    }

    return result;
}

std::optional<QStringList> Paths::evaluateSubdirCallback(const QString& callback_code,
                                                         const Notification& notification) {
    // Initialize Python if not already initialized
    static bool python_initialized = false;
    if (!python_initialized) {
        Py_Initialize();
        python_initialized = true;
    }

    // Build Python dictionary from notification
    PyObject* py_dict = PyDict_New();

    PyDict_SetItemString(py_dict, "app_name",
                         PyUnicode_FromString(notification.app_name.toUtf8().constData()));
    PyDict_SetItemString(py_dict, "summary",
                         PyUnicode_FromString(notification.summary.toUtf8().constData()));
    PyDict_SetItemString(py_dict, "body",
                         PyUnicode_FromString(notification.body.toUtf8().constData()));
    PyDict_SetItemString(py_dict, "app_icon",
                         PyUnicode_FromString(notification.app_icon.toUtf8().constData()));
    PyDict_SetItemString(py_dict, "id", PyLong_FromLong(notification.id));
    PyDict_SetItemString(py_dict, "replaces_id", PyLong_FromLong(notification.replaces_id));
    PyDict_SetItemString(py_dict, "expire_timeout", PyLong_FromLong(notification.expire_timeout));

    // Convert hints to Python dict
    PyObject* py_hints = PyDict_New();
    for (auto it = notification.hints.begin(); it != notification.hints.end(); ++it) {
        QString key = it.key();
        QVariant value = it.value();

        PyObject* py_value = nullptr;
        if (value.type() == QVariant::String) {
            py_value = PyUnicode_FromString(value.toString().toUtf8().constData());
        } else if (value.type() == QVariant::Int || value.type() == QVariant::LongLong) {
            py_value = PyLong_FromLongLong(value.toLongLong());
        } else if (value.type() == QVariant::Bool) {
            py_value = PyBool_FromLong(value.toBool());
        } else {
            py_value = PyUnicode_FromString(value.toString().toUtf8().constData());
        }

        PyDict_SetItemString(py_hints, key.toUtf8().constData(), py_value);
        Py_XDECREF(py_value);
    }
    PyDict_SetItemString(py_dict, "hints", py_hints);
    Py_XDECREF(py_hints);

    // Convert actions to Python dict
    PyObject* py_actions = PyDict_New();
    for (auto it = notification.actions.begin(); it != notification.actions.end(); ++it) {
        PyDict_SetItemString(py_actions, it->first.toUtf8().constData(),
                             PyUnicode_FromString(it->second.toUtf8().constData()));
    }
    PyDict_SetItemString(py_dict, "actions", py_actions);
    Py_XDECREF(py_actions);

    // Evaluate the lambda expression
    QString full_code = QString("result = (%1)").arg(callback_code);
    PyObject* globals = PyDict_New();
    PyDict_SetItemString(globals, "notification", py_dict);

    PyObject* locals = PyDict_New();

    PyRun_String(full_code.toUtf8().constData(), Py_file_input, globals, locals);

    std::optional<QStringList> result;

    if (PyErr_Occurred()) {
        PyErr_Print();
        logger.error("Failed to evaluate subdir_callback");
    } else {
        PyObject* py_result = PyDict_GetItemString(locals, "result");
        if (py_result && PyList_Check(py_result)) {
            QStringList string_list;
            Py_ssize_t size = PyList_Size(py_result);

            bool all_strings = true;
            for (Py_ssize_t i = 0; i < size; ++i) {
                PyObject* item = PyList_GetItem(py_result, i);
                if (PyUnicode_Check(item)) {
                    const char* str = PyUnicode_AsUTF8(item);
                    if (str && strlen(str) > 0) {
                        string_list.append(QString::fromUtf8(str));
                    }
                } else {
                    all_strings = false;
                    break;
                }
            }

            if (all_strings && !string_list.isEmpty()) {
                result = string_list;
            }
        } else if (py_result == Py_None || !py_result) {
            // Return nullopt for None or empty
            result = std::nullopt;
        }
    }

    Py_XDECREF(py_dict);
    Py_XDECREF(globals);
    Py_XDECREF(locals);

    return result;
}

fs::path Paths::getCustomOutputDir(const fs::path& root_path, const fs::path& default_outdir,
                                   const Notification& notification) {
    // Build list of directories to check (from most specific to root)
    std::vector<fs::path> dirs_to_check;
    fs::path current = default_outdir;

    // Add default_outdir and all its parents up to root_path
    while (current != root_path && current.string().find(root_path.string()) == 0) {
        dirs_to_check.push_back(current);
        current = current.parent_path();
    }

    // Reverse to check from root to specific
    std::reverse(dirs_to_check.begin(), dirs_to_check.end());

    // Check each directory for .settings.json with subdir_callback
    for (const auto& dir : dirs_to_check) {
        fs::path settings_file = dir / ".settings.json";

        if (fs::exists(settings_file)) {
            QFile file(QString::fromStdString(settings_file.string()));
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("subdir_callback")) {
                        QString callback = obj["subdir_callback"].toString();

                        logger.debug(QString("Found subdir_callback in %1")
                                         .arg(QString::fromStdString(settings_file.string())));

                        auto subdir_result = evaluateSubdirCallback(callback, notification);

                        if (subdir_result.has_value()) {
                            QStringList subdir_parts = subdir_result.value();

                            // Build the output path
                            fs::path outdir = dir;
                            for (const QString& part : subdir_parts) {
                                outdir = outdir / slugify(part).toStdString();
                            }

                            // Validate that outdir is below dir
                            fs::path canonical_dir = fs::weakly_canonical(dir);
                            fs::path canonical_outdir = fs::weakly_canonical(outdir);

                            std::string dir_str = canonical_dir.string();
                            std::string outdir_str = canonical_outdir.string();

                            if (outdir_str.find(dir_str) == 0) {
                                logger.info(QString("Using custom subdir: %1")
                                                .arg(QString::fromStdString(outdir.string())));
                                return outdir;
                            } else {
                                logger.error(QString("Subdir must be below %1, got %2")
                                                 .arg(QString::fromStdString(dir_str))
                                                 .arg(QString::fromStdString(outdir_str)));
                            }
                        }
                    }
                }
            }
        }
    }

    // Return default if no custom path found
    return default_outdir;
}

fs::path Paths::getOutputPath(const fs::path& root_path, const Notification& notification) {
    QString app_name_slug = slugify(notification.app_name);
    QString summary_slug = slugify(notification.summary);

    // Build default output directory
    fs::path default_outdir = root_path / app_name_slug.toStdString() / summary_slug.toStdString();

    // Check for custom subdir_callback
    fs::path outdir = getCustomOutputDir(root_path, default_outdir, notification);

    // Build filename
    QString run_id = notification.notification_tray_run_id;
    QString id_str = QString::number(notification.id);
    QString suffix = ".json";
    QString name = QString("%1-%2").arg(run_id).arg(id_str);

    // Truncate name if too long
    if (name.length() > MAX_FILENAME_LENGTH - suffix.length()) {
        name = name.left(MAX_FILENAME_LENGTH - suffix.length());
    }

    QString filename = name + suffix;
    fs::path output_path = outdir / filename.toStdString();

    // Truncate full path if too long
    std::string path_str = output_path.string();
    if (path_str.length() > MAX_FILEPATH_LENGTH - suffix.length()) {
        path_str = path_str.substr(0, MAX_FILEPATH_LENGTH - suffix.length());
        output_path = fs::path(path_str + suffix.toStdString());
    }

    return output_path;
}
