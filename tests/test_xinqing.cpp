#include "apiendpoint.h"
#include "mainwindow.h"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

class XinqingTest : public QObject {
    Q_OBJECT
private slots:
    void endpoint_data() {
        QTest::addColumn<QString>("base");
        QTest::addColumn<QString>("chat");
        QTest::addColumn<QString>("models");
        QTest::newRow("local") << QStringLiteral("127.0.0.1:8000") << QStringLiteral("http://127.0.0.1:8000/v1/chat/completions") << QStringLiteral("http://127.0.0.1:8000/v1/models");
        QTest::newRow("version") << QStringLiteral("https://example.com/v1/") << QStringLiteral("https://example.com/v1/chat/completions") << QStringLiteral("https://example.com/v1/models");
        QTest::newRow("proxy") << QStringLiteral("https://example.com/team/api/v1") << QStringLiteral("https://example.com/team/api/v1/chat/completions") << QStringLiteral("https://example.com/team/api/v1/models");
        QTest::newRow("custom") << QStringLiteral("https://example.com/compatible-mode/v1/") << QStringLiteral("https://example.com/compatible-mode/v1/chat/completions") << QStringLiteral("https://example.com/compatible-mode/v1/models");
        QTest::newRow("full") << QStringLiteral("https://example.com/v1/chat/completions") << QStringLiteral("https://example.com/v1/chat/completions") << QStringLiteral("https://example.com/v1/models");
    }
    void endpoint() {
        QFETCH(QString, base); QFETCH(QString, chat); QFETCH(QString, models);
        QCOMPARE(ApiEndpoint::chat(base).toString(), chat);
        QCOMPARE(ApiEndpoint::models(base).toString(), models);
    }
    void streamedChatAndPersistence() {
        QTemporaryDir isolated;
        QVERIFY(isolated.isValid());
        QStandardPaths::setTestModeEnabled(true);
        qputenv("XDG_DATA_HOME", isolated.path().toUtf8());
        QCoreApplication::setApplicationName("XinqingTest");
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        qputenv("XINQING_API_BASE", QString("http://127.0.0.1:%1/team/v1/").arg(server.serverPort()).toUtf8());
        qputenv("XINQING_MODEL", "fixture-model");
        qputenv("XINQING_API_KEY", "fixture-key");
        QByteArray request;
        bool served = false;
        QJsonObject received;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [&, socket] {
                request += socket->readAll();
                const int headerEnd = request.indexOf("\r\n\r\n");
                if (headerEnd < 0 || served) return;
                int contentLength = 0;
                for (const QByteArray &line : request.left(headerEnd).split('\n'))
                    if (line.toLower().startsWith("content-length:")) contentLength = line.mid(15).trimmed().toInt();
                const QByteArray body = request.mid(headerEnd + 4);
                if (body.size() < contentLength) return;
                received = QJsonDocument::fromJson(body.left(contentLength)).object();
                served = true;
                const QString answer = QStringLiteral("{\"response_text\":\"慢慢来，我在听。\",\"emotion_label\":\"relaxed\",\"user_profile\":{\"personality_traits\":[],\"interests\":[],\"style\":[]},\"memory_refs\":[]}");
                QByteArray sse;
                for (int i = 0; i < answer.size(); i += 7) {
                    QJsonObject delta{{"content", answer.mid(i, 7)}};
                    QJsonObject event{{"choices", QJsonArray{QJsonObject{{"delta", delta}}}}};
                    sse += "data: " + QJsonDocument(event).toJson(QJsonDocument::Compact) + "\r\n\r\n";
                }
                sse += "data: [DONE]\r\n\r\n";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: " + QByteArray::number(sse.size()) + "\r\nConnection: close\r\n\r\n");
                socket->write(sse);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        {
            MainWindow window;
            auto *input = window.findChild<QPlainTextEdit*>("m_chatInput");
            auto *send = window.findChild<QPushButton*>("m_btnSend");
            QVERIFY(input); QVERIFY(send);
            input->setPlainText(QStringLiteral("今天有点累"));
            send->click();
            QTRY_VERIFY_WITH_TIMEOUT(served, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(send->isEnabled(), 5000);
            QVERIFY(request.startsWith("POST /team/v1/chat/completions HTTP/1.1"));
            QByteArray authorization;
            for (const QByteArray &line : request.left(request.indexOf("\r\n\r\n")).split('\n'))
                if (line.toLower().startsWith("authorization:")) authorization = line.mid(14).trimmed();
            QCOMPARE(authorization, QByteArray("Bearer fixture-key"));
            QCOMPARE(received.value("model").toString(), QString("fixture-model"));
            QVERIFY(received.value("stream").toBool());
            const auto messages = received.value("messages").toArray();
            QVERIFY(messages[0].toObject().value("content").toString().contains(QStringLiteral("心晴")));
            QVERIFY(messages[1].toObject().value("content").toString().contains(QStringLiteral("今天有点累")));
            bool displayed = false;
            for (auto *label : window.findChildren<QLabel*>())
                if (label->text() == QStringLiteral("慢慢来，我在听。")) displayed = true;
            QVERIFY(displayed);
        }
        QFile saved(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/xinqing_sessions.json");
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QByteArray savedBytes = saved.readAll();
        QVERIFY(savedBytes.contains(QStringLiteral("慢慢来，我在听。").toUtf8()));
        QVERIFY(!savedBytes.contains("fixture-key"));
        qunsetenv("XINQING_API_BASE"); qunsetenv("XINQING_MODEL"); qunsetenv("XINQING_API_KEY");
    }
};
QTEST_MAIN(XinqingTest)
#include "test_xinqing.moc"
