#pragma once
#include <QObject>
#include <QPointer>
#include <QJsonObject>
#include <QWindow>
#include <memory>

// Presentation lifetime is independent of the network worker. Only the selected
// session owns the shared child canvas; switching joins its decoder first.
class SessionPresentation : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
public:
    explicit SessionPresentation(QObject* parent = nullptr);
    ~SessionPresentation() override;
    bool ready() const;
    bool active() const;
    bool hasFrames() const;
    bool configure(const QJsonObject& config);
    void frame(QByteArray bytes);
    void status(const QJsonObject& status);
    void overlays(const QJsonObject& overlays);
    void preedit(const QString& text);
    void attach(QWindow* canvas);
    void detach();
    void resized();
    QJsonObject capabilities() const;
signals:
    void readyChanged();
    void activeChanged();
    void idrRequested();
    void failed(QString error);
private:
    bool start();
    void stop();
    void readyFrame(quint64 generation);
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
