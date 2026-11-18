#include "resultloader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace ResultLoader {

static QStringList toStringList(const QJsonArray &arr)
{
    QStringList out;
    for (const QJsonValue &v : arr)
        out << v.toString();
    return out;
}

QVector<SubmissionRow> loadSubmission(const QString &path, QString *errorOut)
{
    QVector<SubmissionRow> rows;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut)
            *errorOut = QStringLiteral("无法打开文件: %1").arg(path);
        return rows;
    }
    int lineNo = 0;
    while (!file.atEnd()) {
        ++lineNo;
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            if (errorOut)
                *errorOut = QStringLiteral("第 %1 行 JSON 解析失败: %2")
                                .arg(lineNo)
                                .arg(err.errorString());
            return {};
        }
        const QJsonObject obj = doc.object();
        const QJsonObject profile = obj.value(QStringLiteral("user_profile")).toObject();

        SubmissionRow row;
        row.id = obj.value(QStringLiteral("id")).toString();
        row.responseText = obj.value(QStringLiteral("response_text")).toString();
        row.emotionLabel = obj.value(QStringLiteral("emotion_label")).toString();
        row.personalityTraits = toStringList(
            profile.value(QStringLiteral("personality_traits")).toArray());
        row.interests = toStringList(profile.value(QStringLiteral("interests")).toArray());
        row.style = toStringList(profile.value(QStringLiteral("style")).toArray());
        row.memoryRefs = toStringList(obj.value(QStringLiteral("memory_refs")).toArray());
        rows.append(row);
    }
    return rows;
}

QJsonObject loadPerformanceReport(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

QHash<QString, QJsonObject> loadInputsById(const QString &path)
{
    QHash<QString, QJsonObject> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        const QJsonObject obj = doc.object();
        const QString id = obj.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            out.insert(id, obj);
    }
    return out;
}

int countJsonlLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    int n = 0;
    while (!file.atEnd()) {
        if (!file.readLine().trimmed().isEmpty())
            ++n;
    }
    return n;
}

QString emotionZh(const QString &label)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("joy"), QStringLiteral("喜悦")},
        {QStringLiteral("gratitude"), QStringLiteral("感激")},
        {QStringLiteral("relaxed"), QStringLiteral("放松")},
        {QStringLiteral("care"), QStringLiteral("关怀")},
        {QStringLiteral("pride"), QStringLiteral("自豪")},
        {QStringLiteral("neutral"), QStringLiteral("中性")},
        {QStringLiteral("surprise"), QStringLiteral("惊讶")},
        {QStringLiteral("mixed"), QStringLiteral("混合")},
        {QStringLiteral("sadness"), QStringLiteral("悲伤")},
        {QStringLiteral("loneliness"), QStringLiteral("孤独")},
        {QStringLiteral("anxiety"), QStringLiteral("焦虑")},
        {QStringLiteral("anger"), QStringLiteral("愤怒")},
        {QStringLiteral("fear"), QStringLiteral("恐惧")},
        {QStringLiteral("disgust"), QStringLiteral("厌恶")},
        {QStringLiteral("shame"), QStringLiteral("羞愧")},
        {QStringLiteral("helplessness"), QStringLiteral("无助")},
    };
    return map.value(label, label);
}

} // namespace ResultLoader
