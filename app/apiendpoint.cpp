#include "apiendpoint.h"
namespace {
QUrl normalized(QString address)
{
    address = address.trimmed();
    if (!address.startsWith("http://") && !address.startsWith("https://"))
        address.prepend("http://");
    QUrl url(address);
    QString path = url.path();
    while (path.endsWith('/')) path.chop(1);
    url.setPath(path);
    url.setFragment(QString());
    return url;
}
}
QUrl ApiEndpoint::chat(const QString &address)
{
    QUrl url = normalized(address);
    QString path = url.path();
    if (!path.endsWith("/chat/completions")) {
        if (path.isEmpty()) path = "/v1";
        path += "/chat/completions";
    }
    url.setPath(path);
    return url;
}
QUrl ApiEndpoint::models(const QString &address)
{
    QUrl url = chat(address);
    QString path = url.path();
    path.chop(QStringLiteral("/chat/completions").size());
    url.setPath(path + "/models");
    return url;
}
