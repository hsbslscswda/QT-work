#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "resultloader.h"

#include <QColor>
#include <QDateTime>
#include <QMainWindow>
#include <QResizeEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QPair>
#include <QPoint>
#include <QSet>
#include <QTimer>
#include <QVariantAnimation>
#include <QVector>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class ZenMiniBar;

// 单条对话消息
struct ChatMessage {
    QString role;       // "user" or "assistant"
    QString text;       // 实际说话文本
    QString thinking;   // 思维链 (如果有)
    QString emotion;    // 当轮情绪
    QDateTime time;
};

// 独立多会话数据结构
struct ChatSession {
    QString id;
    QString title;
    QDateTime createdAt;
    QDateTime updatedAt;
    QVector<ChatMessage> messages;
    QString lastEmotion; // 本会话当前心境
};

// 极简伴读微浮条窗口（禅模式）
class ZenMiniBar : public QWidget
{
    Q_OBJECT
public:
    explicit ZenMiniBar(QWidget *parent = nullptr);
    void setResponseText(const QString &text);
    void setThinkingState(bool thinking);

signals:
    void sendMessageRequested(const QString &text);
    void restoreMainWindowRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QPoint m_dragPosition;
    QLineEdit *m_miniInput = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_replyPopup = nullptr;
    QTimer *m_popupTimer = nullptr;
};

// 方案A：天际线浸润（Horizon Glow & Atmosphere Header）
// 60FPS 硬件加速顶栏微光画布：在顶栏呈现丝滑流淌的晨曦天色，垂直向下自然羽化渐隐融入正文素雅黑
class AmbientCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit AmbientCanvas(QWidget *parent = nullptr);
    void setStops(const QColor &c0, const QColor &c1, const QColor &c2, const QColor &c3);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_c0{0x2a, 0x2c, 0x34};
    QColor m_c1{0x22, 0x24, 0x2a};
    QColor m_c2{0x1a, 0x1b, 0x1f};
    QColor m_c3{0x13, 0x13, 0x14};
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSendClicked();
    void onClearCurrentChatClicked();
    void onCheckServerClicked();

    void onChatReadyRead();
    void onChatStreamFinished();
    void flushStreamBufferToUi();

    // 多会话管理
    void onNewChatClicked();
    void onSessionItemClicked(QListWidgetItem *item);
    void onSessionContextMenu(const QPoint &pos);
    void toggleSidebar();
    void openSettingsDialog();

    // 创新功能
    void onBreatheTick();
    void openMoodCapsuleDialog();
    void enterZenMode();
    void exitZenMode();
    void handleMiniBarSend(const QString &text);

private:
    Ui::MainWindow *ui;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_chatReply = nullptr;
    bool m_imeComposing = false;

    // 多会话管理引擎
    QVector<ChatSession> m_sessions;
    int m_currentSessionIdx = 0;
    void initMultiSession();
    void createNewSession(const QString &title = QString());
    void switchSession(int index);
    void deleteSession(int index);
    void renderCurrentSessionMessages();
    void refreshSessionListWidget();
    void saveSessionsToFile();
    void loadSessionsFromFile();
    QString sessionsFilePath() const;

    // 侧边栏与主区 UI 控件
    QWidget *m_sidebarWidget = nullptr;
    QPushButton *m_btnToggleSidebar = nullptr;
    QPushButton *m_btnExpandSidebar = nullptr;
    QListWidget *m_sessionList = nullptr;
    AmbientCanvas *m_ambientCanvas = nullptr; // 方案A：顶栏天色浸润画布
    QWidget *m_chatMainArea = nullptr;       // 纯正沉静素雅黑主区容器
    QWidget *m_emptyStateWidget = nullptr;
    QLabel *m_sessionTitleLabel = nullptr;

    // 服务配置
    QString m_serverHost = "http://127.0.0.1:8000";
    QString m_modelName = "Qwen2.5-14B-Instruct";
    QString m_apiKey;
    void loadSettings();
    void saveSettings();

    // 心境流光与呼吸律动（初始偏浅黑单色调渐变 -> 情绪触发后 5.0s 丝滑绽放 4-stop 极光天色）
    QString m_activeEmotion = "neutral";
    QColor m_curStop0{0x2a, 0x2c, 0x34}; // 初始偏浅冷灰黑
    QColor m_curStop1{0x22, 0x24, 0x2a}; // 初始雅炭黑
    QColor m_curStop2{0x1a, 0x1b, 0x1f}; // 初始曜石黑
    QColor m_curStop3{0x13, 0x13, 0x14}; // 初始深底黑

    QColor m_fromStop0, m_targetStop0;
    QColor m_fromStop1, m_targetStop1;
    QColor m_fromStop2, m_targetStop2;
    QColor m_fromStop3, m_targetStop3;

    QVariantAnimation *m_bgAnim = nullptr;
    QTimer *m_breatheTimer = nullptr;
    qreal m_breathePhase = 0.0;
    bool m_userScrolledUp = false;

    void applyEmotionTheme(const QString &emotion);
    void animateBackgroundTo(const QColor &c0, const QColor &c1, const QColor &c2, const QColor &c3);
    void updateBackgroundStyle(const QColor &c0, const QColor &c1, const QColor &c2, const QColor &c3);

    // 伴读微浮条
    ZenMiniBar *m_zenBar = nullptr;

    QString m_systemPrompt;
    void loadSystemPrompt();

    // 流式增量状态
    QByteArray m_sseHttpBuffer;
    QString m_tagBuffer;
    QString m_pendingThinkingChunk;
    QString m_pendingNormalChunk;
    QString m_accumulatedThinking;
    QString m_accumulatedNormal;
    bool m_isThinking = false;
    QTimer *m_uiThrottleTimer = nullptr;

    QLabel *m_currentThinkLabel = nullptr;
    QLabel *m_currentResponseLabel = nullptr;
    QWidget *m_currentThinkContainer = nullptr;

    void createStreamingAssistantBubble();
    void processDeltaText(const QString &delta);

    QString serverBaseUrl() const;
    void appendChatBubble(const QString &role, const QString &text, const QString &thinking = QString());
    int calculateBubbleMaxWidth() const;
};

#endif // MAINWINDOW_H
