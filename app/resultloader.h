#ifndef RESULTLOADER_H
#define RESULTLOADER_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// 对应 submission.jsonl 中的一行
struct SubmissionRow {
    QString id;
    QString responseText;
    QString emotionLabel;
    QStringList personalityTraits;
    QStringList interests;
    QStringList style;
    QStringList memoryRefs;
};

namespace ResultLoader {

// 读取 submission.jsonl；失败时抛出错误信息到 errorOut（可为空）
QVector<SubmissionRow> loadSubmission(const QString &path, QString *errorOut = nullptr);

// 读取 performance_report.json；文件不存在时返回空对象
QJsonObject loadPerformanceReport(const QString &path);

// 把输入 JSONL 按 id 索引，用于详情面板回放对话历史
QHash<QString, QJsonObject> loadInputsById(const QString &path);

// 统计 JSONL 行数（忽略空行）
int countJsonlLines(const QString &path);

// 情绪标签中英对照
QString emotionZh(const QString &label);

} // namespace ResultLoader

#endif // RESULTLOADER_H
