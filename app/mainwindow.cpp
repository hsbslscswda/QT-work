#include "mainwindow.h"
#include "apiendpoint.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QSaveFile>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

// ----------------------------------------------------------------
// 辅助函数：增量提取真实说话文本
// ----------------------------------------------------------------
// ----------------------------------------------------------------
// 辅助函数：跨 Qt5 / Qt6 测量单行文字像素宽度
// ----------------------------------------------------------------
static inline int measureTextWidth(const QFont &font, const QString &text)
{
    QFontMetrics fm(font);
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return fm.horizontalAdvance(text);
#else
    return fm.width(text);
#endif
}

static QString extractStreamingResponse(const QString &raw)
{
    const QString key = "\"response_text\"";
    int idx = raw.indexOf(key);
    if (idx == -1) {
        if (!raw.trimmed().startsWith('{')) {
            return raw;
        }
        return QString();
    }
    int colonIdx = raw.indexOf(':', idx + key.length());
    if (colonIdx == -1) return QString();
    int quoteStart = raw.indexOf('"', colonIdx);
    if (quoteStart == -1) return QString();
    quoteStart += 1;

    QString result;
    result.reserve(raw.length() - quoteStart);
    int i = quoteStart;
    while (i < raw.length()) {
        QChar c = raw[i];
        if (c == '\\') {
            if (i + 1 < raw.length()) {
                QChar nxt = raw[i + 1];
                if (nxt == '"') result.append('"');
                else if (nxt == 'n') result.append('\n');
                else if (nxt == 't') result.append('\t');
                else if (nxt == '\\') result.append('\\');
                else result.append(nxt);
                i += 2;
                continue;
            } else {
                break;
            }
        } else if (c == '"') {
            break;
        } else {
            result.append(c);
            ++i;
        }
    }
    return result;
}

static QString resolveEmotionWithInertia(const QString &prev, const QString &current)
{
    static const QSet<QString> neutralSet = {"neutral", "relaxed", "care", "mixed"};
    static const QSet<QString> strongSet = {
        "joy", "gratitude", "pride", "sadness", "loneliness", 
        "anxiety", "anger", "fear", "disgust", "shame", "helplessness"
    };

    QString c = current.trimmed().toLower();
    QString p = prev.trimmed().toLower();

    if (neutralSet.contains(c) && strongSet.contains(p)) {
        return p;
    }
    return c.isEmpty() ? "neutral" : c;
}

// ----------------------------------------------------------------
// 创新功能 3：伴读微浮条窗口（ZenMiniBar / 禅模式）
// ----------------------------------------------------------------
ZenMiniBar::ZenMiniBar(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(520, 56);

    auto *rootLay = new QHBoxLayout(this);
    rootLay->setContentsMargins(6, 6, 6, 6);

    auto *pillFrame = new QFrame(this);
    pillFrame->setStyleSheet(
        "QFrame { background: rgba(24, 24, 27, 0.95); border: 1.5px solid #444746; "
        "border-radius: 22px; }");
    auto *pillLay = new QHBoxLayout(pillFrame);
    pillLay->setContentsMargins(14, 4, 12, 4);
    pillLay->setSpacing(8);

    auto *logo = new QLabel("🌸 心晴", pillFrame);
    logo->setStyleSheet("color: #c4b5fd; font-weight: bold; font-size: 13px;");
    pillLay->addWidget(logo);

    m_miniInput = new QLineEdit(pillFrame);
    m_miniInput->setPlaceholderText("想对心晴说什么… (Enter 发送)");
    m_miniInput->setStyleSheet(
        "QLineEdit { background: transparent; border: none; color: #ffffff; "
        "font-size: 13px; padding: 2px 4px; }");
    pillLay->addWidget(m_miniInput, 1);

    m_statusLabel = new QLabel("●", pillFrame);
    m_statusLabel->setStyleSheet("color: #10b981; font-size: 10px;");
    pillLay->addWidget(m_statusLabel);

    auto *btnRestore = new QPushButton("⛶", pillFrame);
    btnRestore->setToolTip("展开回完整主窗口");
    btnRestore->setCursor(Qt::PointingHandCursor);
    btnRestore->setStyleSheet(
        "QPushButton { background: transparent; color: #a1a1aa; border: none; "
        "font-size: 14px; padding: 2px 6px; } QPushButton:hover { color: #ffffff; }");
    pillLay->addWidget(btnRestore);

    rootLay->addWidget(pillFrame);

    connect(m_miniInput, &QLineEdit::returnPressed, this, [this] {
        const QString txt = m_miniInput->text().trimmed();
        if (!txt.isEmpty()) {
            emit sendMessageRequested(txt);
            m_miniInput->clear();
        }
    });
    connect(btnRestore, &QPushButton::clicked, this, &ZenMiniBar::restoreMainWindowRequested);

    m_replyPopup = new QLabel(nullptr);
    m_replyPopup->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    m_replyPopup->setAttribute(Qt::WA_TranslucentBackground);
    m_replyPopup->setWordWrap(true);
    m_replyPopup->setStyleSheet(
        "QLabel { background: rgba(30, 31, 32, 0.96); color: #f1f2f4; "
        "border: 1px solid #6366f1; border-radius: 12px; padding: 10px 14px; font-size: 13px; line-height: 1.4; }");
    m_replyPopup->hide();

    m_popupTimer = new QTimer(this);
    m_popupTimer->setSingleShot(true);
    connect(m_popupTimer, &QTimer::timeout, m_replyPopup, &QLabel::hide);
}

void ZenMiniBar::setResponseText(const QString &text)
{
    m_statusLabel->setStyleSheet("color: #10b981; font-size: 10px;");
    m_miniInput->setPlaceholderText("想对心晴说什么… (Enter 发送)");

    if (!text.isEmpty()) {
        m_replyPopup->setText(text);
        m_replyPopup->adjustSize();
        QPoint pos = this->pos();
        m_replyPopup->move(pos.x(), pos.y() - m_replyPopup->height() - 8);
        m_replyPopup->show();
        m_popupTimer->start(8000);
    }
}

void ZenMiniBar::setThinkingState(bool thinking)
{
    if (thinking) {
        m_statusLabel->setStyleSheet("color: #a855f7; font-size: 10px;");
        m_miniInput->setPlaceholderText("心晴正在倾听思考中…");
    }
}

void ZenMiniBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
#else
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
#endif
        event->accept();
    }
}

void ZenMiniBar::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        move(event->globalPosition().toPoint() - m_dragPosition);
#else
        move(event->globalPos() - m_dragPosition);
#endif
        if (m_replyPopup->isVisible()) {
            m_replyPopup->move(x(), y() - m_replyPopup->height() - 8);
        }
        event->accept();
    }
}

// ----------------------------------------------------------------
// 主窗口核心实现
// ----------------------------------------------------------------
// ----------------------------------------------------------------
// 60FPS 极光微光硬件加速底板画布实现（原生 QPainter 绘制，0ms CSS 重解析开销）
// ----------------------------------------------------------------
AmbientCanvas::AmbientCanvas(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void AmbientCanvas::setStops(const QColor &c0, const QColor &c1, const QColor &c2, const QColor &c3)
{
    m_c0 = c0;
    m_c1 = c1;
    m_c2 = c2;
    m_c3 = c3;
    update(); // 原生微秒级绘制，无任何 CSS 样式树重解析，满血 60 FPS！
}

void AmbientCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 1. 横向与微斜向的 4-stop 极光天色流光 (Atmospheric Aurora Sky)
    QLinearGradient skyGrad(0, 0, width(), height());
    skyGrad.setColorAt(0.00, m_c0);
    skyGrad.setColorAt(0.35, m_c1);
    skyGrad.setColorAt(0.70, m_c2);
    skyGrad.setColorAt(1.00, m_c3);
    p.fillRect(rect(), skyGrad);

    // 2. 垂直自然羽化渐隐遮罩（Fade to Obsidian #131314）
    // 顶部透出温润天色，底部平滑完全融入正文素雅黑 (#131314)
    QLinearGradient fadeGrad(0, 0, 0, height());
    fadeGrad.setColorAt(0.00, QColor(0x13, 0x13, 0x14, 20));
    fadeGrad.setColorAt(0.35, QColor(0x13, 0x13, 0x14, 75));
    fadeGrad.setColorAt(0.70, QColor(0x13, 0x13, 0x14, 180));
    fadeGrad.setColorAt(1.00, QColor(0x13, 0x13, 0x14, 255));
    p.fillRect(rect(), fadeGrad);

    // 3. 水天一色线（1px 极简微光下边缘线）
    QLinearGradient horizonLine(0, height() - 1, width(), height() - 1);
    horizonLine.setColorAt(0.0, QColor(255, 255, 255, 10));
    horizonLine.setColorAt(0.2, QColor(255, 255, 255, 45));
    horizonLine.setColorAt(0.5, QColor(255, 255, 255, 60));
    horizonLine.setColorAt(0.8, QColor(255, 255, 255, 45));
    horizonLine.setColorAt(1.0, QColor(255, 255, 255, 10));
    p.setPen(QPen(QBrush(horizonLine), 1));
    p.drawLine(0, height() - 1, width(), height() - 1);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 启动直接进入主聊天工作区
    ui->m_stack->setCurrentWidget(ui->m_pageChat);

    loadSystemPrompt();
    loadSettings();

    // ------------------------------------------------------------
    // 60FPS 满帧 Google 极光渐变动画系统 (5.0秒平缓温和伽马校正微光溶解，杜绝掉帧与突兀)
    // ------------------------------------------------------------
    m_bgAnim = new QVariantAnimation(this);
    m_bgAnim->setDuration(5000); // 5.0秒平缓丝滑过渡
    m_bgAnim->setStartValue(0.0);
    m_bgAnim->setEndValue(1.0);
    m_bgAnim->setEasingCurve(QEasingCurve::InOutCubic);

    connect(m_bgAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &val) {
        qreal t = val.toReal();
        // 伽马校正平方混合（Gamma-corrected perceptual color blending），过渡期色彩鲜活不发灰暗沉
        auto interpolate = [](const QColor &c1, const QColor &c2, qreal ratio) {
            qreal a0 = c1.red() * c1.red();
            qreal b0 = c2.red() * c2.red();
            qreal a1 = c1.green() * c1.green();
            qreal b1 = c2.green() * c2.green();
            qreal a2 = c1.blue() * c1.blue();
            qreal b2 = c2.blue() * c2.blue();
            return QColor(
                qBound(0, static_cast<int>(qSqrt(a0 + (b0 - a0) * ratio) + 0.5), 255),
                qBound(0, static_cast<int>(qSqrt(a1 + (b1 - a1) * ratio) + 0.5), 255),
                qBound(0, static_cast<int>(qSqrt(a2 + (b2 - a2) * ratio) + 0.5), 255)
            );
        };
        m_curStop0 = interpolate(m_fromStop0, m_targetStop0, t);
        m_curStop1 = interpolate(m_fromStop1, m_targetStop1, t);
        m_curStop2 = interpolate(m_fromStop2, m_targetStop2, t);
        m_curStop3 = interpolate(m_fromStop3, m_targetStop3, t);
        updateBackgroundStyle(m_curStop0, m_curStop1, m_curStop2, m_curStop3);
    });

    m_breatheTimer = new QTimer(this);
    connect(m_breatheTimer, &QTimer::timeout, this, &MainWindow::onBreatheTick);
    m_breatheTimer->start(16); // 16ms = 60 FPS 满帧丝滑呼吸刷新

    // ------------------------------------------------------------
    // 全局样式：Gemini 风格素雅深黑与高级交互 (#131314 / #18181b / #1e1f20)
    // ------------------------------------------------------------
    setStyleSheet(R"(
        * {
            color: #e3e3e3;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "PingFang SC", "Microsoft YaHei", sans-serif;
        }
        QMainWindow { background: #131314; }
        QWidget#centralwidget { background: #131314; }
        QWidget#m_pageChat { background: #131314; }

        QPlainTextEdit {
            background: #1e1f20; color: #ffffff; border: 1.5px solid #3c4043;
            border-radius: 14px; padding: 12px; font-size: 14px;
        }
        QPlainTextEdit:focus {
            border: 1.5px solid #a8c7fa; background: #232427;
        }
        QPushButton {
            background: #282a2c; color: #e3e3e3; border: 1px solid #3c4043; border-radius: 8px;
            padding: 8px 18px; font-size: 13px; font-weight: normal;
        }
        QPushButton:hover { background: #37393d; color: #ffffff; border-color: #5e6064; }
        QPushButton:pressed { background: #1f2022; color: #c4c7c5; }
        QLineEdit {
            border: 1.5px solid #3c4043; border-radius: 8px; padding: 6px 10px;
            background: #131314; color: #ffffff; font-size: 13px;
        }
        QLineEdit:focus { border: 1.5px solid #a8c7fa; }
        QLabel { color: #e3e3e3; }
        QStatusBar { color: #8e918f; background: #131314; font-size: 12px; }
    )");

    // ------------------------------------------------------------
    // 重构布局：左侧现代侧边栏 + 右侧主对话流 (QSplitter)
    // ------------------------------------------------------------
    while (ui->mainLayout->count() > 0) {
        QLayoutItem *it = ui->mainLayout->takeAt(0);
        if (it->widget()) it->widget()->hide();
        delete it;
    }

    auto *mainSplitter = new QSplitter(Qt::Horizontal, ui->m_pageChat);
    mainSplitter->setHandleWidth(1);
    mainSplitter->setStyleSheet("QSplitter::handle { background: transparent; }");

    // 1. 左侧侧边栏（无多余边框、无滚动条暗刺）
    m_sidebarWidget = new QWidget(mainSplitter);
    m_sidebarWidget->setFixedWidth(240);
    m_sidebarWidget->setStyleSheet("background: #18181b; border: none;");
    auto *sbLay = new QVBoxLayout(m_sidebarWidget);
    sbLay->setContentsMargins(12, 14, 12, 12);
    sbLay->setSpacing(10);

    // 侧边栏头部
    auto *sbHeader = new QHBoxLayout();
    auto *sbTitle = new QLabel("🌸 心晴", m_sidebarWidget);
    sbTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #c4b5fd;");
    sbHeader->addWidget(sbTitle);
    sbHeader->addStretch();

    m_btnToggleSidebar = new QPushButton("◀", m_sidebarWidget);
    m_btnToggleSidebar->setToolTip("收起侧边栏");
    m_btnToggleSidebar->setCursor(Qt::PointingHandCursor);
    m_btnToggleSidebar->setStyleSheet(
        "QPushButton { background: transparent; color: #71717a; border: none; font-size: 12px; padding: 4px; } "
        "QPushButton:hover { color: #ffffff; }");
    connect(m_btnToggleSidebar, &QPushButton::clicked, this, &MainWindow::toggleSidebar);
    sbHeader->addWidget(m_btnToggleSidebar);
    sbLay->addLayout(sbHeader);

    // + 开启新对话大按钮
    auto *btnNewChat = new QPushButton("+ 开启新心事", m_sidebarWidget);
    btnNewChat->setCursor(Qt::PointingHandCursor);
    btnNewChat->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #7c3aed, stop:1 #6366f1); "
        "color: #ffffff; border: none; border-radius: 10px; padding: 10px 14px; font-size: 13px; font-weight: bold; } "
        "QPushButton:hover { background: #6d28d9; }");
    connect(btnNewChat, &QPushButton::clicked, this, &MainWindow::onNewChatClicked);
    sbLay->addWidget(btnNewChat);

    auto *recentLabel = new QLabel("历史心事轨迹", m_sidebarWidget);
    recentLabel->setStyleSheet("color: #71717a; font-size: 11px; font-weight: bold; margin-top: 6px;");
    sbLay->addWidget(recentLabel);

    // 会话列表（彻底关闭原生外边框与滚动条，消除左上角多余竖线 BUG）
    m_sessionList = new QListWidget(m_sidebarWidget);
    m_sessionList->setFrameShape(QFrame::NoFrame);
    m_sessionList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sessionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sessionList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sessionList->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: none; } "
        "QListWidget::item { padding: 10px 12px; border-radius: 8px; margin-bottom: 4px; color: #cbd5e1; font-size: 13px; } "
        "QListWidget::item:hover { background: #27272a; color: #ffffff; } "
        "QListWidget::item:selected { background: #2e2640; color: #c4b5fd; border-left: 3px solid #7c3aed; }");
    connect(m_sessionList, &QListWidget::itemClicked, this, &MainWindow::onSessionItemClicked);
    connect(m_sessionList, &QListWidget::customContextMenuRequested, this, &MainWindow::onSessionContextMenu);
    sbLay->addWidget(m_sessionList, 1);

    auto *btnSettings = new QPushButton("⚙️ 服务连接配置", m_sidebarWidget);
    btnSettings->setCursor(Qt::PointingHandCursor);
    btnSettings->setStyleSheet(
        "QPushButton { background: #27272a; color: #a1a1aa; border: 1px solid #333538; "
        "border-radius: 8px; padding: 8px 12px; font-size: 12px; } "
        "QPushButton:hover { background: #3f3f46; color: #ffffff; }");
    connect(btnSettings, &QPushButton::clicked, this, &MainWindow::openSettingsDialog);
    sbLay->addWidget(btnSettings);

    mainSplitter->addWidget(m_sidebarWidget);

    // 2. 右侧主工作区：纯粹沉静的素雅黑画布底座 (#131314)
    m_chatMainArea = new QWidget(mainSplitter);
    m_chatMainArea->setObjectName("chatMainArea");
    m_chatMainArea->setStyleSheet("QWidget#chatMainArea { background: #131314; }");
    auto *chatMainLay = new QVBoxLayout(m_chatMainArea);
    chatMainLay->setContentsMargins(0, 0, 0, 12);
    chatMainLay->setSpacing(0);

    // 顶部操作导航条：天际线浸润 (Horizon Glow & Atmosphere Header)
    m_ambientCanvas = new AmbientCanvas(m_chatMainArea);
    m_ambientCanvas->setFixedHeight(68);
    auto *topBarLay = new QHBoxLayout(m_ambientCanvas);
    topBarLay->setContentsMargins(18, 10, 18, 10);
    topBarLay->setSpacing(10);

    m_btnExpandSidebar = new QPushButton("☰ 侧栏", m_ambientCanvas);
    m_btnExpandSidebar->setCursor(Qt::PointingHandCursor);
    m_btnExpandSidebar->setStyleSheet(
        "QPushButton { background: rgba(255, 255, 255, 0.10); color: #f1f2f4; border: 1px solid rgba(255, 255, 255, 0.16); "
        "border-radius: 6px; padding: 4px 10px; font-size: 12px; } "
        "QPushButton:hover { background: rgba(255, 255, 255, 0.18); color: #ffffff; }");
    m_btnExpandSidebar->hide();
    connect(m_btnExpandSidebar, &QPushButton::clicked, this, &MainWindow::toggleSidebar);
    topBarLay->addWidget(m_btnExpandSidebar);

    m_sessionTitleLabel = new QLabel("初见心晴", m_ambientCanvas);
    m_sessionTitleLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #f8fafc;");
    topBarLay->addWidget(m_sessionTitleLabel);

    auto *onlineBadge = new QLabel("● 在线陪伴", m_ambientCanvas);
    onlineBadge->setStyleSheet("color: #6ee7b7; font-size: 12px; font-weight: 500; padding-left: 2px;");
    topBarLay->addWidget(onlineBadge);

    topBarLay->addStretch();

    auto *btnCapsule = new QPushButton("💊 今日心事胶囊", m_ambientCanvas);
    btnCapsule->setCursor(Qt::PointingHandCursor);
    btnCapsule->setToolTip("生成今日诗意拍立得手账并一键保存到本地");
    btnCapsule->setStyleSheet(
        "QPushButton { background: rgba(255, 255, 255, 0.12); color: #ffffff; border: 1px solid rgba(255, 255, 255, 0.20); "
        "border-radius: 8px; padding: 6px 14px; font-size: 12px; font-weight: 500; } "
        "QPushButton:hover { background: rgba(255, 255, 255, 0.22); }");
    connect(btnCapsule, &QPushButton::clicked, this, &MainWindow::openMoodCapsuleDialog);
    topBarLay->addWidget(btnCapsule);

    auto *btnZen = new QPushButton("🌱 伴读微浮条", m_ambientCanvas);
    btnZen->setCursor(Qt::PointingHandCursor);
    btnZen->setToolTip("折叠为桌面悬浮小胶囊，伴读写代码零打扰");
    btnZen->setStyleSheet(
        "QPushButton { background: rgba(255, 255, 255, 0.08); color: #e2e8f0; border: 1px solid rgba(255, 255, 255, 0.14); "
        "border-radius: 8px; padding: 6px 14px; font-size: 12px; } "
        "QPushButton:hover { background: rgba(255, 255, 255, 0.16); color: #ffffff; }");
    connect(btnZen, &QPushButton::clicked, this, &MainWindow::enterZenMode);
    topBarLay->addWidget(btnZen);

    auto *btnClear = new QPushButton("清空本轮", m_ambientCanvas);
    btnClear->setCursor(Qt::PointingHandCursor);
    btnClear->setStyleSheet(
        "QPushButton { background: transparent; color: rgba(255, 255, 255, 0.55); border: none; font-size: 12px; padding: 4px 8px; } "
        "QPushButton:hover { color: #f87171; }");
    connect(btnClear, &QPushButton::clicked, this, &MainWindow::onClearCurrentChatClicked);
    topBarLay->addWidget(btnClear);

    chatMainLay->addWidget(m_ambientCanvas, 0);

    // 聊天视口：全域素雅黑 (#131314)，极高对比度，长时间阅读安宁舒适，无任何刺眼色块
    ui->m_scrollArea->setParent(m_chatMainArea);
    ui->m_scrollArea->setWidgetResizable(true);
    ui->m_scrollArea->setFrameShape(QFrame::NoFrame);
    ui->m_scrollArea->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: #131314; border: none; }");
    if (ui->m_scrollArea->viewport()) {
        ui->m_scrollArea->viewport()->setStyleSheet("background: #131314; border: none;");
    }
    chatMainLay->addWidget(ui->m_scrollArea, 1);

    // 底部输入区：宽阔舒适的倾诉对话框
    auto *inputCard = new QFrame(m_chatMainArea);
    inputCard->setStyleSheet("QFrame { background: #131314; border: none; }");
    auto *inputLay = new QHBoxLayout(inputCard);
    inputLay->setContentsMargins(18, 8, 18, 0);
    inputLay->setSpacing(12);

    ui->m_chatInput->setParent(inputCard);
    ui->m_chatInput->setFixedHeight(92);
    ui->m_chatInput->setPlaceholderText("想对心晴倾诉什么… (Enter 发送 / Shift+Enter 换行)");
    ui->m_chatInput->installEventFilter(this);
    inputLay->addWidget(ui->m_chatInput, 1);

    ui->m_btnSend->setParent(inputCard);
    ui->m_btnSend->setFixedHeight(92);
    ui->m_btnSend->setFixedWidth(92);
    ui->m_btnSend->setCursor(Qt::PointingHandCursor);
    ui->m_btnSend->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #7c3aed, stop:1 #6366f1); "
        "color: #ffffff; border: none; border-radius: 14px; font-size: 15px; font-weight: bold; } "
        "QPushButton:hover { background: #6d28d9; } "
        "QPushButton:disabled { background: #27272a; color: #71717a; }");
    connect(ui->m_btnSend, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    inputLay->addWidget(ui->m_btnSend);

    chatMainLay->addWidget(inputCard, 0);

    mainSplitter->addWidget(m_chatMainArea);
    mainSplitter->setCollapsible(0, true);
    mainSplitter->setSizes({240, 860});
    ui->mainLayout->addWidget(mainSplitter);

    initMultiSession();
    updateBackgroundStyle(m_curStop0, m_curStop1, m_curStop2, m_curStop3);

    // 智能滚动防跳跃系统：监听用户手动滚动意图，解决吐字时屏幕自己往上动无法点收起的问题
    QScrollBar *vBar = ui->m_scrollArea->verticalScrollBar();
    connect(vBar, &QScrollBar::actionTriggered, this, [this](int action) {
        if (action == QScrollBar::SliderSingleStepSub || action == QScrollBar::SliderPageStepSub ||
            action == QScrollBar::SliderMove || action == QScrollBar::SliderToMinimum) {
            m_userScrolledUp = true;
        }
    });
    connect(vBar, &QScrollBar::valueChanged, this, [this](int val) {
        QScrollBar *sb = ui->m_scrollArea->verticalScrollBar();
        if (sb->maximum() - val <= 50) {
            m_userScrolledUp = false; // 用户回到最底部时恢复自动跟随
        }
    });

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    statusBar()->showMessage("心晴就绪 · 随时倾听你的声音");
}

MainWindow::~MainWindow()
{
    saveSessionsToFile();
    if (m_zenBar) {
        delete m_zenBar;
    }
    delete ui;
}

// ----------------------------------------------------------------
// 多会话管理引擎
// ----------------------------------------------------------------
QString MainWindow::sessionsFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/xinqing_sessions.json";
}

void MainWindow::initMultiSession()
{
    loadSessionsFromFile();
    if (m_sessions.isEmpty()) {
        createNewSession("初见心晴");
    } else {
        refreshSessionListWidget();
        switchSession(0);
    }
}

void MainWindow::createNewSession(const QString &title)
{
    ChatSession sess;
    sess.id = QString::number(QDateTime::currentMSecsSinceEpoch());
    sess.title = title.isEmpty() ? QString("新心事 · %1").arg(QDateTime::currentDateTime().toString("MM-dd hh:mm")) : title;
    sess.createdAt = QDateTime::currentDateTime();
    sess.updatedAt = QDateTime::currentDateTime();
    sess.lastEmotion = "neutral";

    m_sessions.prepend(sess);
    refreshSessionListWidget();
    switchSession(0);
}

void MainWindow::switchSession(int index)
{
    if (index < 0 || index >= m_sessions.size())
        return;

    m_currentSessionIdx = index;
    m_sessionList->setCurrentRow(index);
    m_sessionTitleLabel->setText(m_sessions[index].title);

    m_activeEmotion = m_sessions[index].lastEmotion;
    applyEmotionTheme(m_activeEmotion);

    renderCurrentSessionMessages();

    // 确保新会话立即清空、激活输入焦点并可自由打字
    ui->m_chatInput->setEnabled(true);
    ui->m_chatInput->setReadOnly(false);
    ui->m_chatInput->clear();
    ui->m_chatInput->setFocus();
    ui->m_btnSend->setEnabled(true);
}

void MainWindow::deleteSession(int index)
{
    if (m_chatReply || index < 0 || index >= m_sessions.size())
        return;

    if (m_sessions.size() == 1) {
        m_sessions[0].messages.clear();
        m_sessions[0].title = "初见心晴";
        m_sessions[0].lastEmotion = "neutral";
        refreshSessionListWidget();
        switchSession(0);
        saveSessionsToFile();
        return;
    }

    m_sessions.removeAt(index);
    refreshSessionListWidget();
    int newIdx = qMin(index, m_sessions.size() - 1);
    switchSession(newIdx);
    saveSessionsToFile();
}

void MainWindow::refreshSessionListWidget()
{
    const QSignalBlocker blocker(m_sessionList);
    m_sessionList->clear();
    for (int i = 0; i < m_sessions.size(); ++i) {
        const auto &s = m_sessions.at(i);
        auto *item = new QListWidgetItem(QString("💬 %1").arg(s.title), m_sessionList);
        item->setToolTip(QString("创建于: %1\n共 %2 条对话").arg(s.createdAt.toString("yyyy-MM-dd hh:mm"), QString::number(s.messages.size())));
        m_sessionList->addItem(item);
    }
}

void MainWindow::onNewChatClicked()
{
    if (m_chatReply) {
        auto *reply = m_chatReply;
        m_chatReply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
        auto &messages = m_sessions[m_currentSessionIdx].messages;
        if (!messages.isEmpty() && messages.last().role == "user")
            messages.removeLast();
    }
    if (m_uiThrottleTimer) m_uiThrottleTimer->stop();

    ui->m_btnSend->setEnabled(true);
    ui->m_chatInput->setEnabled(true);
    ui->m_chatInput->setReadOnly(false);
    ui->m_chatInput->clear();

    createNewSession();
    ui->m_chatInput->setFocus();
}

void MainWindow::onSessionItemClicked(QListWidgetItem *item)
{
    int row = m_sessionList->row(item);
    if (row != m_currentSessionIdx) {
        if (m_chatReply) {
            QMessageBox::information(this, "请稍候", "正在回复中，请稍候切换会话。");
            m_sessionList->setCurrentRow(m_currentSessionIdx);
            return;
        }
        switchSession(row);
    }
}

void MainWindow::onSessionContextMenu(const QPoint &pos)
{
    if (m_chatReply) {
        statusBar()->showMessage("回复期间暂不能删除会话", 5000);
        return;
    }
    auto *item = m_sessionList->itemAt(pos);
    if (!item) return;
    int row = m_sessionList->row(item);

    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #1e1f20; color: #f1f2f4; border: 1px solid #333538; } "
                       "QMenu::item:selected { background: #27272a; }");
    auto *delAct = menu.addAction("🗑️ 删除该会话");
    connect(delAct, &QAction::triggered, this, [this, row] {
        deleteSession(row);
    });
    menu.exec(m_sessionList->mapToGlobal(pos));
}

void MainWindow::toggleSidebar()
{
    bool visible = m_sidebarWidget->isVisible();
    m_sidebarWidget->setVisible(!visible);
    m_btnExpandSidebar->setVisible(visible);
}

// ----------------------------------------------------------------
// 渲染当前会话消息 (Gemini 风格：无生硬突兀卡片框，与背景浑然一体)
// ----------------------------------------------------------------
void MainWindow::renderCurrentSessionMessages()
{
    while (ui->m_chatLayout->count() > 0) {
        QLayoutItem *it = ui->m_chatLayout->takeAt(0);
        if (it->widget()) delete it->widget();
        delete it;
    }

    m_currentThinkLabel = nullptr;
    m_currentResponseLabel = nullptr;
    m_currentThinkContainer = nullptr;

    const auto &msgs = m_sessions[m_currentSessionIdx].messages;

    if (msgs.isEmpty()) {
        auto *emptyBox = new QWidget();
        emptyBox->setObjectName("emptyCard");
        auto *eLay = new QVBoxLayout(emptyBox);
        eLay->setContentsMargins(20, 40, 20, 20);
        eLay->setSpacing(14);
        eLay->setAlignment(Qt::AlignCenter);

        auto *icon = new QLabel("🌸", emptyBox);
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet("font-size: 38px; background: none;");
        eLay->addWidget(icon);

        auto *title = new QLabel("今天有什么想对心晴说的？", emptyBox);
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet("font-size: 18px; font-weight: bold; color: #f8fafc; background: none;");
        eLay->addWidget(title);

        auto *sub = new QLabel("倾诉任何疲惫、喜悦与困惑，心晴永远在这里静静陪伴你。", emptyBox);
        sub->setAlignment(Qt::AlignCenter);
        sub->setStyleSheet("font-size: 13px; color: #94a3b8; background: none; margin-bottom: 12px;");
        eLay->addWidget(sub);

        const QStringList starters = {
            "“今天写代码赶报告真的好累好烦呀…”",
            "“今天遇到了一件很开心的小事想分享给你！”",
            "“感觉有点迷茫，想找人随便漫谈两句”"
        };
        for (const QString &st : starters) {
            auto *btn = new QPushButton(st, emptyBox);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(
                "QPushButton { background: rgba(30, 31, 32, 0.85); color: #cbd5e1; border: 1px solid #333538; "
                "border-radius: 12px; padding: 10px 18px; font-size: 13px; } "
                "QPushButton:hover { background: #27272a; color: #c4b5fd; border-color: #6366f1; }");
            connect(btn, &QPushButton::clicked, this, [this, st] {
                QString cleaned = st;
                cleaned.remove("“").remove("”");
                ui->m_chatInput->setPlainText(cleaned);
                onSendClicked();
            });
            eLay->addWidget(btn);
        }

        ui->m_chatLayout->addWidget(emptyBox);
    } else {
        for (const auto &m : msgs) {
            appendChatBubble(m.role, m.text, m.thinking);
        }
    }

    ui->m_chatLayout->addStretch();
}

void MainWindow::saveSessionsToFile()
{
    QJsonArray sArr;
    for (const auto &s : m_sessions) {
        QJsonObject sObj;
        sObj["id"] = s.id;
        sObj["title"] = s.title;
        sObj["createdAt"] = s.createdAt.toString(Qt::ISODate);
        sObj["lastEmotion"] = s.lastEmotion;

        QJsonArray mArr;
        for (const auto &m : s.messages) {
            QJsonObject mObj;
            mObj["role"] = m.role;
            mObj["text"] = m.text;
            mObj["thinking"] = m.thinking;
            mObj["emotion"] = m.emotion;
            mObj["time"] = m.time.toString(Qt::ISODate);
            mArr.append(mObj);
        }
        sObj["messages"] = mArr;
        sArr.append(sObj);
    }

    QJsonObject root;
    root["sessions"] = sArr;

    const auto data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QSaveFile file(sessionsFilePath());
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        statusBar()->showMessage(QString("会话记录保存失败：%1").arg(file.errorString()), 8000);
}

void MainWindow::loadSessionsFromFile()
{
    m_sessions.clear();
    QFile f(sessionsFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;

    for (const auto &sv : doc.object().value("sessions").toArray()) {
        QJsonObject so = sv.toObject();
        ChatSession s;
        s.id = so.value("id").toString();
        s.title = so.value("title").toString();
        s.createdAt = QDateTime::fromString(so.value("createdAt").toString(), Qt::ISODate);
        s.lastEmotion = so.value("lastEmotion").toString();

        for (const auto &mv : so.value("messages").toArray()) {
            QJsonObject mo = mv.toObject();
            ChatMessage m;
            m.role = mo.value("role").toString();
            m.text = mo.value("text").toString();
            m.thinking = mo.value("thinking").toString();
            m.emotion = mo.value("emotion").toString();
            m.time = QDateTime::fromString(mo.value("time").toString(), Qt::ISODate);
            s.messages.append(m);
        }
        m_sessions.append(s);
    }
}

void MainWindow::openSettingsDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("心晴 · 大模型服务连接配置与在线检测");
    dlg.setFixedSize(560, 440);
    dlg.setStyleSheet("QDialog { background: #18181b; color: #f1f2f4; } QLabel { color: #f1f2f4; font-size: 13px; }");

    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(24, 20, 24, 20);
    lay->setSpacing(14);

    auto *title = new QLabel("⚙️ 大模型服务连接配置 (支持 DeepSeek / 咪咕 / OpenAI 兼容接口)", &dlg);
    title->setStyleSheet("font-size: 15px; font-weight: bold; color: #c4b5fd; margin-bottom: 2px;");
    lay->addWidget(title);

    auto *grid = new QGridLayout();
    grid->setSpacing(10);

    // 快捷平台预设下拉框
    grid->addWidget(new QLabel("服务平台预设:", &dlg), 0, 0);
    auto *presetCombo = new QComboBox(&dlg);
    presetCombo->setStyleSheet(
        "QComboBox { background: #27272a; color: white; border: 1.5px solid #3f3f46; border-radius: 8px; padding: 6px 10px; font-size: 13px; } "
        "QComboBox QAbstractItemView { background: #27272a; color: white; selection-background-color: #7c3aed; }");
    presetCombo->addItem("自定义配置 / 本地 vLLM");
    presetCombo->addItem("DeepSeek 官方 API (deepseek-flash · 最新 V4.1 Flash/思考/1M上下文)");
    presetCombo->addItem("DeepSeek 官方 API (deepseek-v4-pro · 最新 V4 Pro 专家旗舰)");
    presetCombo->addItem("DeepSeek 旧版兼容 (deepseek-chat / deepseek-reasoner)");
    presetCombo->addItem("通义千问 / 硅基流动 / OpenAI 兼容接口");
    grid->addWidget(presetCombo, 0, 1);

    grid->addWidget(new QLabel("API 地址:", &dlg), 1, 0);
    auto *hostEdit = new QLineEdit(m_serverHost, &dlg);
    hostEdit->setPlaceholderText("例如: https://api.deepseek.com 或 咪咕8000端口链接");
    hostEdit->setStyleSheet("QLineEdit { background: #27272a; color: white; border: 1.5px solid #3f3f46; border-radius: 8px; padding: 8px; font-size: 13px; } QLineEdit:focus { border-color: #a8c7fa; }");
    grid->addWidget(hostEdit, 1, 1);

    grid->addWidget(new QLabel("模型名称:", &dlg), 2, 0);
    auto *modelEdit = new QLineEdit(m_modelName, &dlg);
    modelEdit->setPlaceholderText("例如: deepseek-chat 或 Qwen2.5-14B-Instruct");
    modelEdit->setStyleSheet("QLineEdit { background: #27272a; color: white; border: 1.5px solid #3f3f46; border-radius: 8px; padding: 8px; font-size: 13px; } QLineEdit:focus { border-color: #a8c7fa; }");
    grid->addWidget(modelEdit, 2, 1);

    grid->addWidget(new QLabel("API Key (密钥):", &dlg), 3, 0);
    auto *keyEdit = new QLineEdit(m_apiKey, &dlg);
    keyEdit->setPlaceholderText("调用 DeepSeek 商业 API 时填入 sk-...(咪咕平台内网请留空)");
    keyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    keyEdit->setStyleSheet("QLineEdit { background: #27272a; color: white; border: 1.5px solid #3f3f46; border-radius: 8px; padding: 8px; font-size: 13px; } QLineEdit:focus { border-color: #a8c7fa; }");
    grid->addWidget(keyEdit, 3, 1);

    lay->addLayout(grid);

    // 下拉框联动：同步 DeepSeek 2026 最新模型生态 (V4.1 Flash / V4 Pro)
    connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [hostEdit, modelEdit, keyEdit](int idx) {
        if (idx == 1) { // DeepSeek-V4.1 Flash
            hostEdit->setText("https://api.deepseek.com");
            modelEdit->setText("deepseek-flash");
            if (keyEdit->text().isEmpty()) keyEdit->setPlaceholderText("必须填入你的 DeepSeek API Key (sk-...)");
            keyEdit->setFocus();
        } else if (idx == 2) { // DeepSeek-V4 Pro
            hostEdit->setText("https://api.deepseek.com");
            modelEdit->setText("deepseek-v4-pro");
            if (keyEdit->text().isEmpty()) keyEdit->setPlaceholderText("必须填入你的 DeepSeek API Key (sk-...)");
            keyEdit->setFocus();
        } else if (idx == 3) { // Legacy R1/V3
            hostEdit->setText("https://api.deepseek.com");
            modelEdit->setText("deepseek-chat");
            keyEdit->setFocus();
        } else if (idx == 0) {
            if (hostEdit->text().contains("deepseek")) {
                hostEdit->setText("http://127.0.0.1:8000");
                modelEdit->setText("Qwen2.5-14B-Instruct");
            }
        }
    });

    // 核心检测服务连接组件
    auto *statusBox = new QFrame(&dlg);
    statusBox->setStyleSheet("QFrame { background: #1e1f20; border: 1px solid #333538; border-radius: 10px; }");
    auto *statusLay = new QHBoxLayout(statusBox);
    statusLay->setContentsMargins(14, 10, 14, 10);
    statusLay->setSpacing(12);

    auto *statusLabel = new QLabel("服务状态：未检测", statusBox);
    statusLabel->setStyleSheet("color: #a1a1aa; font-size: 13px; font-weight: 500;");
    statusLay->addWidget(statusLabel, 1);

    auto *btnCheck = new QPushButton("🔍 检测连接状态", statusBox);
    btnCheck->setCursor(Qt::PointingHandCursor);
    btnCheck->setStyleSheet(
        "QPushButton { background: #2e2640; color: #c4b5fd; border: 1px solid #7c3aed; "
        "border-radius: 6px; padding: 8px 16px; font-size: 12px; font-weight: bold; } "
        "QPushButton:hover { background: #7c3aed; color: #ffffff; }");
    statusLay->addWidget(btnCheck);

    lay->addWidget(statusBox);

    // 检测连接逻辑：智能自适应 DeepSeek / 商业 API 与 咪咕/vLLM 私有服务
    connect(btnCheck, &QPushButton::clicked, &dlg, [this, hostEdit, keyEdit, statusLabel, btnCheck] {
        QString host = hostEdit->text().trimmed();
        QString apiKey = keyEdit->text().trimmed();

        if (host.isEmpty()) {
            statusLabel->setText("❌ 请先输入 API 地址");
            statusLabel->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
            return;
        }

        QString base = host.startsWith("http://") || host.startsWith("https://") ? host : "http://" + host;
        const bool isDeepSeek = base.contains("deepseek.com", Qt::CaseInsensitive);

        if (isDeepSeek && apiKey.isEmpty()) {
            statusLabel->setText("❌ DeepSeek 官方 API 必须填写 API Key 鉴权密钥 (sk-...)！");
            statusLabel->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
            return;
        }

        btnCheck->setEnabled(false);
        btnCheck->setText("检测中…");
        statusLabel->setText(isDeepSeek ? "正在向 DeepSeek 官方鉴权验证…" : "正在连接模型服务器…");
        statusLabel->setStyleSheet("color: #f59e0b; font-size: 13px; font-weight: 500;");

        const QUrl testUrl = ApiEndpoint::models(base);

        if (!m_nam) m_nam = new QNetworkAccessManager(this);
        QNetworkRequest req(testUrl);
        req.setTransferTimeout(8000);
        if (!apiKey.isEmpty()) {
            req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        }

        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, statusLabel, [this, reply, statusLabel, btnCheck, base, apiKey, isDeepSeek] {
            btnCheck->setEnabled(true);
            btnCheck->setText("🔍 检测连接状态");
            reply->deleteLater();

            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

            if (reply->error() == QNetworkReply::NoError || (statusCode >= 200 && statusCode < 300)) {
                if (isDeepSeek) {
                    statusLabel->setText("● 验证成功：已连上 DeepSeek 官方大模型 API，鉴权通过！");
                } else {
                    statusLabel->setText("● 已连上模型列表接口；请发送消息确认对话可用。");
                }
                statusLabel->setStyleSheet("color: #10b981; font-size: 13px; font-weight: bold;");
                statusBar()->showMessage("服务状态：已成功连接并就绪", 5000);
            } else if (statusCode == 401) {
                statusLabel->setText("❌ 鉴权失败 (401)：请检查 API Key 和服务权限。");
                statusLabel->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
            } else {
                if (statusCode == 404 && !isDeepSeek) {
                    QUrl mUrl(base);
                    mUrl.setPath("/health");
                    QNetworkRequest mReq(mUrl);
                    mReq.setTransferTimeout(6000);
                    if (!apiKey.isEmpty()) {
                        mReq.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
                    }
                    QNetworkReply *mReply = m_nam->get(mReq);
                    connect(mReply, &QNetworkReply::finished, statusLabel, [mReply, statusLabel] {
                        mReply->deleteLater();
                        if (mReply->error() == QNetworkReply::NoError) {
                            statusLabel->setText("● 健康检查通过；请发送消息确认模型可用。");
                            statusLabel->setStyleSheet("color: #10b981; font-size: 13px; font-weight: bold;");
                        } else {
                            statusLabel->setText(QString("● 连接异常：%1").arg(mReply->errorString()));
                            statusLabel->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
                        }
                    });
                    return;
                }
                statusLabel->setText(QString("● 连接失败：%1").arg(reply->errorString()));
                statusLabel->setStyleSheet("color: #ef4444; font-size: 13px; font-weight: bold;");
            }
        });
    });

    lay->addStretch();

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto *btnCancel = new QPushButton("取消", &dlg);
    btnCancel->setStyleSheet("QPushButton { background: #27272a; color: #a1a1aa; border: 1px solid #3f3f46; border-radius: 8px; padding: 8px 18px; font-size: 13px; } QPushButton:hover { color: #ffffff; }");
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    btnRow->addWidget(btnCancel);

    auto *btnSave = new QPushButton("保存并应用", &dlg);
    btnSave->setStyleSheet("QPushButton { background: #6366f1; color: white; border: none; border-radius: 8px; padding: 8px 24px; font-size: 13px; font-weight: bold; } QPushButton:hover { background: #4f46e5; }");
    connect(btnSave, &QPushButton::clicked, &dlg, [this, hostEdit, modelEdit, keyEdit, &dlg] {
        m_serverHost = hostEdit->text().trimmed();
        m_modelName = modelEdit->text().trimmed();
        m_apiKey = keyEdit->text().trimmed();
        saveSettings();
        statusBar()->showMessage("服务配置已保存并生效", 4000);
        dlg.accept();
    });
    btnRow->addWidget(btnSave);

    lay->addLayout(btnRow);

    dlg.exec();
}

void MainWindow::loadSettings()
{
    const QString p = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.json";
    QFile f(p);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_serverHost = o.value("host").toString(m_serverHost);
        m_modelName = o.value("model").toString(m_modelName);
        m_apiKey = o.value("api_key").toString(m_apiKey);
    }
    // Session-only overrides for shared/demo machines and automated checks.
    if (qEnvironmentVariableIsSet("XINQING_API_BASE"))
        m_serverHost = qEnvironmentVariable("XINQING_API_BASE");
    if (qEnvironmentVariableIsSet("XINQING_MODEL"))
        m_modelName = qEnvironmentVariable("XINQING_MODEL");
    if (qEnvironmentVariableIsSet("XINQING_API_KEY"))
        m_apiKey = qEnvironmentVariable("XINQING_API_KEY");
}

void MainWindow::saveSettings()
{
    const QString p = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.json";
    QJsonObject o;
    o["host"] = m_serverHost;
    o["model"] = m_modelName;
    o["api_key"] = m_apiKey;
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) {
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
}

QString MainWindow::serverBaseUrl() const
{
    if (m_serverHost.startsWith("http://") || m_serverHost.startsWith("https://"))
        return m_serverHost;
    return "http://" + m_serverHost;
}

// ----------------------------------------------------------------
// 心境流光渐变系统 (3.2秒深呼吸慢速过渡)
// ----------------------------------------------------------------
void MainWindow::onBreatheTick()
{
    m_breathePhase += 0.005; // 60 FPS 满帧丝滑呼吸步长 (~5.2秒自然呼吸周期)
    if (m_breathePhase > 2 * M_PI) {
        m_breathePhase -= 2 * M_PI;
    }

    qreal factor = 0.5 * (1.0 + qSin(m_breathePhase));
    QColor b0 = m_curStop0.lighter(100 + static_cast<int>(factor * 12));
    QColor b1 = m_curStop1.lighter(100 + static_cast<int>(factor * 10));
    QColor b2 = m_curStop2.lighter(100 + static_cast<int>(factor * 8));
    QColor b3 = m_curStop3.lighter(100 + static_cast<int>(factor * 6));

    updateBackgroundStyle(b0, b1, b2, b3);
}

void MainWindow::applyEmotionTheme(const QString &emotion)
{
    m_activeEmotion = resolveEmotionWithInertia(m_activeEmotion, emotion);

    const QString e = m_activeEmotion;
    QColor t0, t1, t2, t3;

    if (e == "joy" || e == "gratitude" || e == "pride" || e == "surprise") {
        // 暖阳微醺·蜜桃落日玫瑰与晨曦琥珀（喜悦/温暖/感激）：邻近暖色层叠，柔和不刺眼
        t0 = QColor(0x3e, 0x1d, 0x33); // 蜜桃玫瑰紫
        t1 = QColor(0x4a, 0x24, 0x1c); // 落日晨曦橙
        t2 = QColor(0x38, 0x1b, 0x42); // 暖意紫晶
        t3 = QColor(0x1c, 0x13, 0x22); // 暮云暗底
    } else if (e == "sadness" || e == "loneliness" || e == "helplessness") {
        // 深海星芒·极光青蓝与暮夜幽靛（治愈安抚/陪伴低落）：深邃静谧的冷色流光
        t0 = QColor(0x16, 0x34, 0x46); // 宝石青湖
        t1 = QColor(0x1e, 0x25, 0x54); // 极光幽蓝
        t2 = QColor(0x28, 0x1d, 0x48); // 暮夜浅紫
        t3 = QColor(0x10, 0x13, 0x22); // 曜石深底
    } else if (e == "anxiety" || e == "fear" || e == "disgust") {
        // 雨后冷杉·清凉青竹薄荷与海雾碧蓝（平复焦虑/舒展呼吸）：清凉澄澈的自然翡翠绿雾
        t0 = QColor(0x14, 0x3e, 0x34); // 鼠尾草绿
        t1 = QColor(0x16, 0x44, 0x4a); // 薄荷翡翠
        t2 = QColor(0x18, 0x34, 0x4c); // 海雾青蓝
        t3 = QColor(0x0e, 0x1a, 0x17); // 冷杉深底
    } else if (e == "anger" || e == "shame") {
        // 暮霭紫晶·黑加仑桑葚红抚平心绪（疏导怒意/温柔拥抱）：沉稳雅致的暖暗宝石色
        t0 = QColor(0x40, 0x1a, 0x36); // 桑葚红紫
        t1 = QColor(0x46, 0x18, 0x23); // 沉香绛红
        t2 = QColor(0x2c, 0x18, 0x3c); // 夜兰幽紫
        t3 = QColor(0x16, 0x10, 0x1b); // 丝绒深底
    } else {
        // 初始与日常平和状态：偏浅黑色的单一灰黑平静渐变（尚未产生强情绪，纯粹极简雅致）
        t0 = QColor(0x24, 0x26, 0x2b); // 偏浅冷灰黑
        t1 = QColor(0x1d, 0x1e, 0x22); // 雅炭黑
        t2 = QColor(0x16, 0x17, 0x1a); // 深曜黑
        t3 = QColor(0x12, 0x12, 0x14); // 纯净深底黑
    }

    animateBackgroundTo(t0, t1, t2, t3);
}

void MainWindow::animateBackgroundTo(const QColor &c0, const QColor &c1, const QColor &c2, const QColor &c3)
{
    if (m_bgAnim->state() == QAbstractAnimation::Running) {
        m_bgAnim->stop();
    }

    m_fromStop0 = m_curStop0;
    m_fromStop1 = m_curStop1;
    m_fromStop2 = m_curStop2;
    m_fromStop3 = m_curStop3;

    m_targetStop0 = c0;
    m_targetStop1 = c1;
    m_targetStop2 = c2;
    m_targetStop3 = c3;

    m_bgAnim->start();
}

void MainWindow::updateBackgroundStyle(const QColor &c0, const QColor &c1, const QColor &c2, const QColor &c3)
{
    // 60FPS 极速渲染：直接由硬件加速的 AmbientCanvas 原生绘制，0 毫秒 CSS 解析开销！
    if (m_ambientCanvas) {
        m_ambientCanvas->setStops(c0, c1, c2, c3);
    }
}

// ----------------------------------------------------------------
// 创新功能 2：今日心事胶囊 & 拍立得手账美图导出
// ----------------------------------------------------------------
void MainWindow::openMoodCapsuleDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("今日心事胶囊 · 心晴手账");
    dlg.setFixedSize(440, 580);
    dlg.setStyleSheet("QDialog { background: #131314; }");

    auto *dlgLay = new QVBoxLayout(&dlg);
    dlgLay->setContentsMargins(18, 18, 18, 18);
    dlgLay->setSpacing(12);

    auto *cardWidget = new QWidget(&dlg);
    cardWidget->setObjectName("polaroidCard");
    cardWidget->setStyleSheet(
        "QWidget#polaroidCard { background: #1e1f20; border: 1.5px solid #333538; "
        "border-radius: 16px; }");
    auto *cardLay = new QVBoxLayout(cardWidget);
    cardLay->setContentsMargins(18, 18, 18, 18);
    cardLay->setSpacing(14);

    auto *header = new QHBoxLayout();
    auto *dateLabel = new QLabel(QDate::currentDate().toString("yyyy 年 MM 月 dd 日 · 心晴陪伴手账"), cardWidget);
    dateLabel->setStyleSheet("color: #94a3b8; font-size: 12px; font-weight: bold;");
    header->addWidget(dateLabel);
    header->addStretch();
    cardLay->addLayout(header);

    auto *photoFrame = new QFrame(cardWidget);
    photoFrame->setFixedHeight(210);
    photoFrame->setStyleSheet(QString(
        "QFrame { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:0.5 %2, stop:1 %3); "
        "border-radius: 12px; border: 1px solid rgba(255, 255, 255, 0.1); }"
    ).arg(m_curStop0.name(), m_curStop1.name(), m_curStop3.name()));

    auto *photoLay = new QVBoxLayout(photoFrame);
    photoLay->setContentsMargins(16, 16, 16, 16);
    photoLay->addStretch();

    auto *stampLabel = new QLabel("✦ 晚霞很温柔，你也是 ✦", photoFrame);
    stampLabel->setAlignment(Qt::AlignCenter);
    stampLabel->setStyleSheet("color: #ffffff; font-size: 16px; font-weight: bold; background: none;");
    photoLay->addWidget(stampLabel);

    auto *statsLabel = new QLabel(QString("当前会话已共鸣 %1 条心事").arg(m_sessions[m_currentSessionIdx].messages.size()), photoFrame);
    statsLabel->setAlignment(Qt::AlignCenter);
    statsLabel->setStyleSheet("color: rgba(255, 255, 255, 0.85); font-size: 12px; background: none;");
    photoLay->addWidget(statsLabel);
    photoLay->addStretch();

    cardLay->addWidget(photoFrame);

    QString noteText;
    if (m_activeEmotion == "sadness" || m_activeEmotion == "loneliness") {
        noteText = "“今天奔波很辛苦，但别把烦恼带到明天。先去喝一杯温水，允许自己偶尔停歇。”";
    } else if (m_activeEmotion == "joy" || m_activeEmotion == "gratitude") {
        noteText = "“心里藏着微光，眼里便有银河。愿这份雀跃与喜悦，温暖你今后的每一个晴天。”";
    } else if (m_activeEmotion == "anxiety" || m_activeEmotion == "fear") {
        noteText = "“把紧绷的心弦悄悄放下，深呼吸。无论发生什么，都有人在安静处一直为你守候。”";
    } else {
        noteText = "“平凡的日子里，藏着细水长流的从容。今天过得平静，也是一种小小的幸福。”";
    }

    auto *quoteLabel = new QLabel(noteText, cardWidget);
    quoteLabel->setWordWrap(true);
    quoteLabel->setStyleSheet("color: #e2e8f0; font-size: 13px; line-height: 1.5; font-style: italic;");
    cardLay->addWidget(quoteLabel);

    auto *signLabel = new QLabel("— 心晴 · 始终在安静处为你亮灯", cardWidget);
    signLabel->setAlignment(Qt::AlignRight);
    signLabel->setStyleSheet("color: #71717a; font-size: 11px;");
    cardLay->addWidget(signLabel);

    dlgLay->addWidget(cardWidget, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto *btnSave = new QPushButton("💾 保存拍立得手账图", &dlg);
    btnSave->setCursor(Qt::PointingHandCursor);
    btnSave->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #7c3aed, stop:1 #6366f1); "
        "color: white; border: none; border-radius: 8px; padding: 8px 20px; font-weight: bold; font-size: 13px; } "
        "QPushButton:hover { background: #6d28d9; }");
    connect(btnSave, &QPushButton::clicked, &dlg, [cardWidget, &dlg] {
        QString savePath = QFileDialog::getSaveFileName(&dlg, "保存心事手账", "心晴心事手账.png", "PNG 图片 (*.png)");
        if (!savePath.isEmpty()) {
            QPixmap pix = cardWidget->grab();
            if (pix.save(savePath)) {
                QMessageBox::information(&dlg, "保存成功", "今日心事手账已成功保存到你的电脑！");
            }
        }
    });
    btnRow->addWidget(btnSave);

    auto *btnClose = new QPushButton("关闭", &dlg);
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addWidget(btnClose);

    dlgLay->addLayout(btnRow);

    dlg.exec();
}

// ----------------------------------------------------------------
// 创新功能 3：伴读微浮条（禅模式）
// ----------------------------------------------------------------
void MainWindow::enterZenMode()
{
    if (!m_zenBar) {
        m_zenBar = new ZenMiniBar();
        connect(m_zenBar, &ZenMiniBar::sendMessageRequested, this, &MainWindow::handleMiniBarSend);
        connect(m_zenBar, &ZenMiniBar::restoreMainWindowRequested, this, &MainWindow::exitZenMode);
    }

    this->hide();
    m_zenBar->move(QApplication::primaryScreen()->geometry().width() - 560,
                   QApplication::primaryScreen()->geometry().height() - 140);
    m_zenBar->show();
}

void MainWindow::exitZenMode()
{
    if (m_zenBar) {
        m_zenBar->hide();
    }
    this->showNormal();
    this->activateWindow();
}

void MainWindow::handleMiniBarSend(const QString &text)
{
    ui->m_chatInput->setPlainText(text);
    if (m_zenBar) {
        m_zenBar->setThinkingState(true);
    }
    onSendClicked();
}

void MainWindow::loadSystemPrompt()
{
    QFile file(":/prompts/system_prompt.txt");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_systemPrompt = QString::fromUtf8(file.readAll());
        file.close();
        return;
    }
    // 本地开发目录回退
    QFile localFile(QCoreApplication::applicationDirPath() + "/prompts/system_prompt.txt");
    if (localFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_systemPrompt = QString::fromUtf8(localFile.readAll());
        localFile.close();
        return;
    }
    QFile relFile("prompts/system_prompt.txt");
    if (relFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_systemPrompt = QString::fromUtf8(relFile.readAll());
        relFile.close();
        return;
    }
    m_systemPrompt = "你是「心晴」，一名温暖、稳定、善解人意且值得信赖的数字情感陪伴者。";
}

// ----------------------------------------------------------------
// 对话发送与流式渲染
// ----------------------------------------------------------------
void MainWindow::onSendClicked()
{
    const QString text = ui->m_chatInput->toPlainText().trimmed();
    if (text.isEmpty()) return;

    m_userScrolledUp = false; // 用户发送新消息，重置滚动锚定并自动滚向最新

    if (m_serverHost.isEmpty() || serverBaseUrl() == "http://") {
        QMessageBox::warning(this, "提示", "请先在侧边栏底部点击「服务连接配置」，填入咪咕实例的 API 地址！");
        return;
    }

    if (m_chatReply) {
        QMessageBox::information(this, "请稍候", "心晴正在回复中，请稍候…");
        return;
    }

    auto &curSess = m_sessions[m_currentSessionIdx];
    if (curSess.messages.isEmpty()) {
        QString autoTitle = text.section('\n', 0, 0).trimmed();
        if (autoTitle.length() > 14) autoTitle = autoTitle.left(14) + "…";
        curSess.title = autoTitle;
        m_sessionTitleLabel->setText(autoTitle);
        refreshSessionListWidget();
        m_sessionList->setCurrentRow(m_currentSessionIdx);
    }

    if (curSess.messages.isEmpty()) {
        while (ui->m_chatLayout->count() > 0) {
            QLayoutItem *it = ui->m_chatLayout->takeAt(0);
            if (it->widget()) delete it->widget();
            delete it;
        }
        ui->m_chatLayout->addStretch();
    }

    ChatMessage uMsg;
    uMsg.role = "user";
    uMsg.text = text;
    uMsg.time = QDateTime::currentDateTime();
    curSess.messages.append(uMsg);

    appendChatBubble("user", text);
    ui->m_chatInput->clear();

    QStringList historyLines;
    historyLines << "样本 ID: chat";
    historyLines << "对话历史:";
    for (const auto &m : curSess.messages) {
        historyLines << QString("%1: %2").arg(m.role, m.text);
    }
    const QString userContent = historyLines.join("\n");

    QJsonObject systemMsg;
    systemMsg["role"] = "system";
    systemMsg["content"] = m_systemPrompt;

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userContent;

    QJsonArray messages;
    messages.append(systemMsg);
    messages.append(userMsg);

    QJsonObject payload;
    payload["model"] = m_modelName;
    payload["messages"] = messages;
    payload["temperature"] = 0.7;
    payload["max_tokens"] = 1024;
    payload["stream"] = true;

    const QUrl baseUrl = ApiEndpoint::chat(serverBaseUrl());
    QNetworkRequest req(baseUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_apiKey.isEmpty()) {
        req.setRawHeader("Authorization", ("Bearer " + m_apiKey.trimmed()).toUtf8());
    }
    // DeepSeek R1 深度思考通常需要更充裕的思考等待时间 (最长 120s)
    req.setTransferTimeout(120000);

    m_sseHttpBuffer.clear();
    m_tagBuffer.clear();
    m_pendingThinkingChunk.clear();
    m_pendingNormalChunk.clear();
    m_accumulatedThinking.clear();
    m_accumulatedNormal.clear();
    m_isThinking = false;

    createStreamingAssistantBubble();

    ui->m_btnSend->setEnabled(false);
    statusBar()->showMessage("心晴正在倾听思考中…");

    if (!m_uiThrottleTimer) {
        m_uiThrottleTimer = new QTimer(this);
        connect(m_uiThrottleTimer, &QTimer::timeout, this, &MainWindow::flushStreamBufferToUi);
    }
    m_uiThrottleTimer->start(30);

    m_chatReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_chatReply, &QNetworkReply::readyRead, this, &MainWindow::onChatReadyRead);
    connect(m_chatReply, &QNetworkReply::finished, this, &MainWindow::onChatStreamFinished);
}

void MainWindow::onChatReadyRead()
{
    if (!m_chatReply) return;
    m_sseHttpBuffer.append(m_chatReply->readAll());

    while (true) {
        int delimiterIdx = m_sseHttpBuffer.indexOf("\n\n");
        int delimiterLen = 2;
        int crlfIdx = m_sseHttpBuffer.indexOf("\r\n\r\n");
        if (crlfIdx != -1 && (delimiterIdx == -1 || crlfIdx < delimiterIdx)) {
            delimiterIdx = crlfIdx;
            delimiterLen = 4;
        }

        if (delimiterIdx == -1)
            break;

        QByteArray frame = m_sseHttpBuffer.left(delimiterIdx).trimmed();
        m_sseHttpBuffer.remove(0, delimiterIdx + delimiterLen);

        if (frame.isEmpty()) continue;

        const auto lines = frame.split('\n');
        for (QByteArray line : lines) {
            line = line.trimmed();
            if (!line.startsWith("data:")) continue;

            QByteArray dataContent = line.mid(5).trimmed();
            if (dataContent == "[DONE]") {
                return;
            }

            QJsonParseError err{};
            QJsonDocument doc = QJsonDocument::fromJson(dataContent, &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonArray choices = doc.object().value("choices").toArray();
                if (!choices.isEmpty()) {
                    QJsonObject delta = choices.first().toObject().value("delta").toObject();
                    
                    if (delta.contains("reasoning_content")) {
                        QString reasonDelta = delta.value("reasoning_content").toString();
                        if (!reasonDelta.isEmpty()) {
                            m_pendingThinkingChunk += reasonDelta;
                            m_accumulatedThinking += reasonDelta;
                        }
                    }

                    if (delta.contains("content")) {
                        QString contentDelta = delta.value("content").toString();
                        if (!contentDelta.isEmpty()) {
                            processDeltaText(contentDelta);
                        }
                    }
                }
            }
        }
    }
}

void MainWindow::processDeltaText(const QString &delta)
{
    // 一旦开始接收常规正文输出，思考过程立即自动收起为精致折叠栏，不遮挡正文
    if (m_currentThinkContainer && m_currentThinkLabel && m_currentThinkLabel->isVisible()) {
        m_currentThinkLabel->setVisible(false);
        QPushButton *btn = m_currentThinkContainer->findChild<QPushButton*>("btnToggleThink");
        if (btn) {
            btn->setText(QString("▶ 💭 思考过程 (已折叠 · %1字 · 点击展开)").arg(m_accumulatedThinking.trimmed().length()));
        }
    }

    m_tagBuffer += delta;

    while (!m_tagBuffer.isEmpty()) {
        if (!m_isThinking) {
            int thinkStart = m_tagBuffer.indexOf("<think>");
            if (thinkStart != -1) {
                if (thinkStart > 0) {
                    QString normalPart = m_tagBuffer.left(thinkStart);
                    m_pendingNormalChunk += normalPart;
                    m_accumulatedNormal += normalPart;
                }
                m_tagBuffer.remove(0, thinkStart + 7);
                m_isThinking = true;
            } else {
                int lastLt = m_tagBuffer.lastIndexOf('<');
                if (lastLt != -1 && (m_tagBuffer.length() - lastLt) < 7) {
                    if (lastLt > 0) {
                        QString safePart = m_tagBuffer.left(lastLt);
                        m_pendingNormalChunk += safePart;
                        m_accumulatedNormal += safePart;
                        m_tagBuffer.remove(0, lastLt);
                    }
                    break;
                } else {
                    m_pendingNormalChunk += m_tagBuffer;
                    m_accumulatedNormal += m_tagBuffer;
                    m_tagBuffer.clear();
                }
            }
        } else {
            int thinkEnd = m_tagBuffer.indexOf("</think>");
            if (thinkEnd != -1) {
                QString thinkPart = m_tagBuffer.left(thinkEnd);
                m_pendingThinkingChunk += thinkPart;
                m_accumulatedThinking += thinkPart;
                m_tagBuffer.remove(0, thinkEnd + 8);
                m_isThinking = false;
            } else {
                int lastLt = m_tagBuffer.lastIndexOf('<');
                if (lastLt != -1 && (m_tagBuffer.length() - lastLt) < 8) {
                    if (lastLt > 0) {
                        QString safePart = m_tagBuffer.left(lastLt);
                        m_pendingThinkingChunk += safePart;
                        m_accumulatedThinking += safePart;
                        m_tagBuffer.remove(0, lastLt);
                    }
                    break;
                } else {
                    m_pendingThinkingChunk += m_tagBuffer;
                    m_accumulatedThinking += m_tagBuffer;
                    m_tagBuffer.clear();
                }
            }
        }
    }
}

void MainWindow::flushStreamBufferToUi()
{
    int maxW = calculateBubbleMaxWidth();

    if (!m_pendingThinkingChunk.isEmpty() && m_currentThinkLabel) {
        if (m_currentThinkContainer && !m_currentThinkContainer->isVisible()) {
            m_currentThinkContainer->setVisible(true);
        }
        m_currentThinkLabel->setText(m_accumulatedThinking);
        m_currentThinkLabel->setFixedWidth(maxW);

        // 思考中默认严格收起！只在有质感的大号折叠胶囊按键上展示进度，绝不暴涨高度霸屏
        QPushButton *btn = m_currentThinkContainer->findChild<QPushButton*>("btnToggleThink");
        if (btn) {
            if (!m_currentThinkLabel->isVisible()) {
                btn->setText(QString("💭 思考过程 · 已折叠 (%1 字 · 点击查看思维链)  ▶").arg(m_accumulatedThinking.trimmed().length()));
            } else {
                btn->setText(QString("💭 思考过程 (共 %1 字)  ▼ 点击收起").arg(m_accumulatedThinking.trimmed().length()));
            }
        }
        m_pendingThinkingChunk.clear();
    }

    if (!m_pendingNormalChunk.isEmpty() && m_currentResponseLabel) {
        QString spokenText = extractStreamingResponse(m_accumulatedNormal);
        if (!spokenText.isEmpty()) {
            m_currentResponseLabel->setText(spokenText);
            int maxLineW = 0;
            for (const QString &line : spokenText.split('\n')) {
                int w = measureTextWidth(m_currentResponseLabel->font(), line);
                if (w > maxLineW) maxLineW = w;
            }
            int w = qMin(maxW, qMax(maxLineW + 24, 200));
            m_currentResponseLabel->setFixedWidth(w);
        }
        m_pendingNormalChunk.clear();
    }

    // 智能滚动防跳跃：只有当用户未向上翻看且未在思考区域互动时，才平滑跟滚到底部；彻底解决打字跳屏导致点不到收起的问题
    if (!m_userScrolledUp) {
        QScrollBar *sb = ui->m_scrollArea->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

void MainWindow::onChatStreamFinished()
{
    if (m_uiThrottleTimer) m_uiThrottleTimer->stop();
    flushStreamBufferToUi();

    QNetworkReply *reply = m_chatReply;
    m_chatReply = nullptr;
    reply->deleteLater();

    ui->m_btnSend->setEnabled(true);
    ui->m_chatInput->setEnabled(true);
    ui->m_chatInput->setReadOnly(false);
    ui->m_chatInput->setFocus();
    statusBar()->showMessage("心晴就绪");

    auto &curSess = m_sessions[m_currentSessionIdx];

    if (reply->error() != QNetworkReply::NoError) {
        if (m_currentResponseLabel) {
            m_currentResponseLabel->setText(
                QString("（连接模型中断/失败：%1）").arg(reply->errorString()));
        }
        if (!curSess.messages.isEmpty() && curSess.messages.last().role == "user") {
            curSess.messages.removeLast();
        }
        return;
    }

    QString content = m_accumulatedNormal.trimmed();
    content.remove(QRegularExpression("^```(?:json)?\\s*", QRegularExpression::CaseInsensitiveOption));
    content.remove(QRegularExpression("\\s*```$"));

    QJsonParseError err{};
    QJsonDocument predDoc = QJsonDocument::fromJson(content.toUtf8(), &err);
    QJsonObject pred = predDoc.object();

    SubmissionRow row;
    row.responseText = pred.value("response_text").toString().trimmed();
    row.emotionLabel = pred.value("emotion_label").toString();

    QString finalSpeech;
    if (!row.responseText.isEmpty()) {
        finalSpeech = row.responseText;
    } else {
        finalSpeech = extractStreamingResponse(m_accumulatedNormal);
        if (finalSpeech.isEmpty()) finalSpeech = m_accumulatedNormal;
    }

    if (m_currentResponseLabel) {
        m_currentResponseLabel->setText(finalSpeech);
    }

    ChatMessage aMsg;
    aMsg.role = "assistant";
    aMsg.text = finalSpeech;
    aMsg.thinking = m_accumulatedThinking;
    aMsg.emotion = row.emotionLabel;
    aMsg.time = QDateTime::currentDateTime();
    curSess.messages.append(aMsg);

    if (m_zenBar && m_zenBar->isVisible()) {
        m_zenBar->setResponseText(finalSpeech);
    }

    if (!row.emotionLabel.isEmpty()) {
        curSess.lastEmotion = row.emotionLabel;
        applyEmotionTheme(row.emotionLabel);
    }

    saveSessionsToFile();

    if (m_currentThinkContainer) {
        if (m_currentThinkLabel) {
            m_currentThinkLabel->setVisible(false); // 结束时彻底折叠收起
        }
        QPushButton *btn = m_currentThinkContainer->findChild<QPushButton*>("btnToggleThink");
        if (btn) {
            btn->setText(QString("💭 思考过程 · 已折叠 (%1 字 · 点击查看思维链)  ▶").arg(m_accumulatedThinking.trimmed().length()));
        }
    }
}

// 助手流式回复气泡（Gemini 风格：无生硬异色框框，纯净融入背景画布）
void MainWindow::createStreamingAssistantBubble()
{
    int maxW = ui->m_scrollArea->viewport()->width() * 4 / 5;

    auto *row = new QWidget();
    row->setStyleSheet("background: transparent; border: none;");
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(12, 6, 12, 6);
    rowLay->setSpacing(0);

    auto *bubbleBox = new QWidget(row);
    bubbleBox->setStyleSheet("background: transparent; border: none;");
    auto *boxLay = new QVBoxLayout(bubbleBox);
    boxLay->setContentsMargins(0, 0, 0, 0);
    boxLay->setSpacing(4);

    m_currentThinkContainer = new QFrame();
    m_currentThinkContainer->setObjectName("thinkCard");
    m_currentThinkContainer->setStyleSheet(
        "QFrame#thinkCard { "
        "background: rgba(35, 30, 52, 0.6); "
        "border: 1px solid rgba(167, 139, 250, 0.35); "
        "border-radius: 12px; padding: 4px 6px; margin-bottom: 6px; } "
        "QFrame#thinkCard:hover { "
        "background: rgba(45, 38, 65, 0.75); border-color: rgba(196, 181, 253, 0.55); }"
    );
    auto *thinkLay = new QVBoxLayout(m_currentThinkContainer);
    thinkLay->setContentsMargins(4, 4, 4, 4);
    thinkLay->setSpacing(4);

    // 稍微大一点、更加精致易点击的折叠头部按钮
    auto *btnToggleThink = new QPushButton("💭 思考过程 · 已折叠 (点击查看思维链)  ▶", m_currentThinkContainer);
    btnToggleThink->setObjectName("btnToggleThink");
    btnToggleThink->setCursor(Qt::PointingHandCursor);
    btnToggleThink->setFixedHeight(36);
    btnToggleThink->setStyleSheet(
        "QPushButton { background: rgba(124, 58, 237, 0.22); color: #c4b5fd; "
        "border: 1px solid rgba(167, 139, 250, 0.45); border-radius: 9px; "
        "font-size: 13px; font-weight: bold; text-align: left; padding: 0px 16px; } "
        "QPushButton:hover { background: rgba(124, 58, 237, 0.42); color: #ffffff; border-color: #a78bfa; }"
    );
    thinkLay->addWidget(btnToggleThink);

    m_currentThinkLabel = new QLabel(m_currentThinkContainer);
    m_currentThinkLabel->setObjectName("thinkLabel");
    m_currentThinkLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_currentThinkLabel->setWordWrap(true);
    m_currentThinkLabel->setStyleSheet("color: #cbd5e1; font-size: 12px; font-style: italic; line-height: 1.5; padding: 6px 6px;");
    m_currentThinkLabel->setVisible(false); // 关键：默认 100% 绝对收起！
    thinkLay->addWidget(m_currentThinkLabel);

    connect(btnToggleThink, &QPushButton::clicked, m_currentThinkContainer, [btnToggleThink, this] {
        if (!m_currentThinkLabel) return;
        bool willShow = !m_currentThinkLabel->isVisible();
        m_currentThinkLabel->setVisible(willShow);
        if (willShow) {
            btnToggleThink->setText(QString("💭 思考过程 (共 %1 字)  ▼ 点击收起").arg(m_accumulatedThinking.trimmed().length()));
        } else {
            btnToggleThink->setText(QString("💭 思考过程 · 已折叠 (%1 字 · 点击查看思维链)  ▶").arg(m_accumulatedThinking.trimmed().length()));
        }
    });

    boxLay->addWidget(m_currentThinkContainer);
    m_currentThinkContainer->setVisible(false);

    m_currentResponseLabel = new QLabel("…");
    m_currentResponseLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_currentResponseLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_currentResponseLabel->setWordWrap(true);
    QFont f = m_currentResponseLabel->font();
    f.setPixelSize(14);
    m_currentResponseLabel->setFont(f);
    m_currentResponseLabel->setStyleSheet(
        "QLabel { background: transparent; color: #e3e3e3; border: none; "
        "padding: 6px 4px; line-height: 1.6; font-size: 14px; }");
    m_currentResponseLabel->setMaximumWidth(maxW);
    boxLay->addWidget(m_currentResponseLabel);

    rowLay->addWidget(bubbleBox);
    rowLay->addStretch();

    ui->m_chatLayout->insertWidget(ui->m_chatLayout->count() - 1, row);
}

void MainWindow::onClearCurrentChatClicked()
{
    if (m_chatReply) {
        QMessageBox::information(this, "请稍候", "心晴正在回复中，暂不能清空。");
        return;
    }

    auto &curSess = m_sessions[m_currentSessionIdx];
    curSess.messages.clear();
    curSess.lastEmotion = "neutral";
    m_activeEmotion = "neutral";
    applyEmotionTheme("neutral");

    renderCurrentSessionMessages();
    saveSessionsToFile();
    statusBar()->showMessage("本轮心事已清空，我们重新开始吧", 5000);
}

void MainWindow::onCheckServerClicked()
{
    openSettingsDialog();
}

// ----------------------------------------------------------------
// 气泡渲染：Gemini 风格（用户是素雅深灰胶囊，助手是自然流动的正文）
// ----------------------------------------------------------------
void MainWindow::appendChatBubble(const QString &role, const QString &text, const QString &thinking)
{
    int maxW = calculateBubbleMaxWidth();

    auto *row = new QWidget();
    row->setStyleSheet("background: transparent; border: none;");
    auto *rowLay = new QHBoxLayout(row);
    rowLay->setContentsMargins(12, 6, 12, 6);
    rowLay->setSpacing(0);

    if (role == "user") {
        auto *bubble = new QLabel(text, row);
        bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bubble->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        QFont f = bubble->font();
        f.setPixelSize(14);
        bubble->setFont(f);
        bubble->setStyleSheet(
            "QLabel { background: #282a2c; color: #f8fafc; "
            "border: 1px solid #3c4043; padding: 10px 18px; border-radius: 18px; line-height: 1.5; }");
        bubble->setWordWrap(true);

        // 依据文本实际测量宽度智能自适应：短句短胶囊，长文充分伸展（最高达 960px），告别狭窄挤密
        int maxLineW = 0;
        for (const QString &line : text.split('\n')) {
            int w = measureTextWidth(f, line);
            if (w > maxLineW) maxLineW = w;
        }
        int bubbleW = qMin(maxW, qMax(maxLineW + 42, 60));
        bubble->setFixedWidth(bubbleW);

        rowLay->addStretch();
        rowLay->addWidget(bubble);
    } else {
        auto *bubbleBox = new QWidget(row);
        bubbleBox->setStyleSheet("background: transparent; border: none;");
        auto *boxLay = new QVBoxLayout(bubbleBox);
        boxLay->setContentsMargins(0, 0, 0, 0);
        boxLay->setSpacing(4);

        if (!thinking.isEmpty()) {
            auto *tFrame = new QFrame(bubbleBox);
            tFrame->setObjectName("thinkCard");
            tFrame->setStyleSheet(
                "QFrame#thinkCard { "
                "background: rgba(35, 30, 52, 0.6); "
                "border: 1px solid rgba(167, 139, 250, 0.35); "
                "border-radius: 12px; padding: 4px 6px; margin-bottom: 6px; } "
                "QFrame#thinkCard:hover { "
                "background: rgba(45, 38, 65, 0.75); border-color: rgba(196, 181, 253, 0.55); }"
            );
            auto *tLay = new QVBoxLayout(tFrame);
            tLay->setContentsMargins(4, 4, 4, 4);
            tLay->setSpacing(4);

            auto *btnToggle = new QPushButton(QString("💭 思考过程 · 已折叠 (%1 字 · 点击查看思维链)  ▶").arg(thinking.trimmed().length()), tFrame);
            btnToggle->setCursor(Qt::PointingHandCursor);
            btnToggle->setFixedHeight(34);
            btnToggle->setStyleSheet(
                "QPushButton { background: rgba(124, 58, 237, 0.22); color: #c4b5fd; "
                "border: 1px solid rgba(167, 139, 250, 0.45); border-radius: 8px; "
                "font-size: 13px; font-weight: bold; text-align: left; padding: 0px 16px; } "
                "QPushButton:hover { background: rgba(124, 58, 237, 0.42); color: #ffffff; border-color: #a78bfa; }"
            );
            tLay->addWidget(btnToggle);

            auto *tLabel = new QLabel(thinking, tFrame);
            tLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            tLabel->setWordWrap(true);
            tLabel->setStyleSheet("color: #cbd5e1; font-size: 12px; font-style: italic; line-height: 1.5; padding: 6px 6px;");
            tLabel->setVisible(false); // 历史记录中默认严格折叠！
            tLay->addWidget(tLabel);

            connect(btnToggle, &QPushButton::clicked, tFrame, [btnToggle, tLabel, thinking] {
                bool willShow = !tLabel->isVisible();
                tLabel->setVisible(willShow);
                if (willShow) {
                    btnToggle->setText(QString("💭 思考过程 (共 %1 字)  ▼ 点击收起").arg(thinking.trimmed().length()));
                } else {
                    btnToggle->setText(QString("💭 思考过程 · 已折叠 (%1 字 · 点击查看思维链)  ▶").arg(thinking.trimmed().length()));
                }
            });

            boxLay->addWidget(tFrame);
        }

        auto *bubble = new QLabel(text, bubbleBox);
        bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bubble->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        QFont f = bubble->font();
        f.setPixelSize(14);
        bubble->setFont(f);
        bubble->setStyleSheet(
            "QLabel { background: transparent; color: #e3e3e3; border: none; "
            "padding: 6px 4px; line-height: 1.6; font-size: 14px; }");
        bubble->setWordWrap(true);

        int maxLineW = 0;
        for (const QString &line : text.split('\n')) {
            int w = measureTextWidth(f, line);
            if (w > maxLineW) maxLineW = w;
        }
        int bubbleW = qMin(maxW, qMax(maxLineW + 24, 200));
        bubble->setFixedWidth(bubbleW);
        boxLay->addWidget(bubble);

        rowLay->addWidget(bubbleBox);
        rowLay->addStretch();
    }

    ui->m_chatLayout->insertWidget(ui->m_chatLayout->count() - 1, row);
    QScrollBar *sb = ui->m_scrollArea->verticalScrollBar();
    sb->setValue(sb->maximum());
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->m_chatInput) {
        if (event->type() == QEvent::InputMethod) {
            auto *ime = static_cast<QInputMethodEvent *>(event);
            m_imeComposing = !ime->preeditString().isEmpty();
            return false;
        } else if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            const bool isEnter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
            if (isEnter) {
                if ((key->modifiers() & Qt::ShiftModifier) || m_imeComposing)
                    return false;
                onSendClicked();
                return true;
            }
            m_imeComposing = false;
            return false;
        } else if (event->type() == QEvent::KeyRelease) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
                return true;
            return false;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

int MainWindow::calculateBubbleMaxWidth() const
{
    int viewportW = 900;
    if (ui && ui->m_scrollArea && ui->m_scrollArea->viewport()) {
        int w = ui->m_scrollArea->viewport()->width();
        if (w > 300) viewportW = w;
    }
    // 允许气泡占据视口 75% 的宽度，最高可扩展至 960px，保底至少 620px
    return qBound(620, static_cast<int>(viewportW * 0.75), 960);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    int maxW = calculateBubbleMaxWidth();
    if (m_currentResponseLabel) {
        m_currentResponseLabel->setMaximumWidth(maxW);
    }
}
