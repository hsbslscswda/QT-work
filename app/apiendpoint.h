#pragma once
#include <QString>
#include <QUrl>
namespace ApiEndpoint {
// Accept a service root, an API base path, or a full chat endpoint.
QUrl chat(const QString &address);
QUrl models(const QString &address);
}
