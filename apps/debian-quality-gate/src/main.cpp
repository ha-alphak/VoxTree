#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDebug>
#include <QGuiApplication>
#include <QLabel>
#include <QLocale>
#include <QMetaObject>
#include <QPushButton>
#include <QTextEdit>
#include <QTranslator>
#include <QUuid>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QWidget>
#include <array>
#include <cstdint>
#include <hvc/client/linux_evdev_input.hpp>
#include <hvc/livekit/livekit_voice_transport.hpp>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace
{
constexpr auto portal_service = "org.freedesktop.portal.Desktop";
constexpr auto portal_path = "/org/freedesktop/portal/desktop";
constexpr auto shortcuts_interface = "org.freedesktop.portal.GlobalShortcuts";
constexpr auto request_interface = "org.freedesktop.portal.Request";
constexpr auto session_interface = "org.freedesktop.portal.Session";

struct PortalShortcut final
{
    QString id;
    QVariantMap options;
};

using PortalShortcuts = QList<PortalShortcut>;
} // namespace

Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(PortalShortcuts)

namespace
{

auto operator<<(QDBusArgument& argument, const PortalShortcut& shortcut) -> QDBusArgument&
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.options;
    argument.endStructure();
    return argument;
}

auto operator>>(const QDBusArgument& argument, PortalShortcut& shortcut) -> const QDBusArgument&
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.options;
    argument.endStructure();
    return argument; // NOLINT(bugprone-return-const-ref-from-parameter)
}

[[nodiscard]] auto portalToken(QStringView prefix) -> QString
{
    auto random = QUuid::createUuid().toString(QUuid::WithoutBraces);
    random.remove(u'-');
    return prefix.toString() + random;
}

class GlobalShortcutPortal final : public QObject
{
    Q_OBJECT

  public:
    explicit GlobalShortcutPortal(QObject* parent = nullptr)
        : QObject(parent), bus_(QDBusConnection::sessionBus())
    {
        qDBusRegisterMetaType<PortalShortcut>();
        qDBusRegisterMetaType<PortalShortcuts>();

        QDBusInterface portal{portal_service, portal_path, shortcuts_interface, bus_};
        available_ = bus_.isConnected() && portal.isValid();
        if (available_)
        {
            version_ = portal.property("version").toUInt();
            static_cast<void>(
                bus_.connect(portal_service, portal_path, shortcuts_interface, "Activated", this,
                             SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap))));
            static_cast<void>(bus_.connect(
                portal_service, portal_path, shortcuts_interface, "Deactivated", this,
                SLOT(onDeactivated(QDBusObjectPath, QString, qulonglong, QVariantMap))));
        }
    }

    ~GlobalShortcutPortal() override
    {
        closeSession();
    }

    [[nodiscard]] auto available() const noexcept -> bool
    {
        return available_;
    }

    [[nodiscard]] auto version() const noexcept -> unsigned int
    {
        return version_;
    }

    void requestShortcuts()
    {
        if (!available_ || pending_operation_ != PendingOperation::none)
        {
            return;
        }
        closeSession();

        QVariantMap options;
        const auto handle_token = portalToken(u"hvc_request_");
        options.insert("handle_token", handle_token);
        options.insert("session_handle_token", portalToken(u"hvc_session_"));
        pending_operation_ = PendingOperation::create_session;
        startRequest("CreateSession", {options}, handle_token);
    }

  signals:
    // NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
    void statusChanged(const QString& status);
    // NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
    void shortcutStateChanged(const QString& shortcut_id, bool pressed, bool background);

  private slots:
    void onPortalResponse(unsigned int response, const QVariantMap& results)
    {
        static_cast<void>(bus_.disconnect(portal_service, request_path_, request_interface,
                                          "Response", this,
                                          SLOT(onPortalResponse(uint, QVariantMap))));
        request_path_.clear();

        const auto operation = std::exchange(pending_operation_, PendingOperation::none);
        if (response != 0U)
        {
            emit statusChanged(
                QStringLiteral("Portal request denied or cancelled (%1).").arg(response));
            return;
        }
        if (operation == PendingOperation::create_session)
        {
            auto session_value = results.value("session_handle");
            if (session_value.canConvert<QDBusVariant>())
            {
                session_value = qvariant_cast<QDBusVariant>(session_value).variant();
            }
            if (session_value.canConvert<QDBusArgument>())
            {
                const auto argument = qvariant_cast<QDBusArgument>(session_value);
                if (argument.currentType() == QDBusArgument::VariantType)
                {
                    QDBusVariant wrapped_value;
                    argument >> wrapped_value;
                    session_value = wrapped_value.variant();
                }
                else
                {
                    QDBusObjectPath object_path;
                    argument >> object_path;
                    session_value = QVariant::fromValue(object_path);
                }
            }
            session_path_ = session_value.metaType() == QMetaType::fromType<QString>()
                                ? session_value.toString()
                                : qvariant_cast<QDBusObjectPath>(session_value).path();
            if (session_path_.isEmpty())
            {
                emit statusChanged(
                    QStringLiteral("Portal returned no shortcut session (keys: %1; type: %2).")
                        .arg(results.keys().join(u','),
                             QString::fromLatin1(session_value.typeName())));
                return;
            }
            bindShortcuts();
        }
        else if (operation == PendingOperation::bind_shortcuts)
        {
            emit statusChanged(
                QStringLiteral("Global PTT shortcuts active; focus another application and test."));
        }
    }

    void onActivated(const QDBusObjectPath& session, const QString& shortcut_id, qulonglong,
                     const QVariantMap&)
    {
        if (session.path() == session_path_)
        {
            emit shortcutStateChanged(shortcut_id, true, applicationIsBackground());
        }
    }

    void onDeactivated(const QDBusObjectPath& session, const QString& shortcut_id, qulonglong,
                       const QVariantMap&)
    {
        if (session.path() == session_path_)
        {
            emit shortcutStateChanged(shortcut_id, false, applicationIsBackground());
        }
    }

  private: // NOLINT(readability-redundant-access-specifiers)
    enum class PendingOperation : std::uint8_t
    {
        none,
        create_session,
        bind_shortcuts
    };

    [[nodiscard]] auto asyncPortalCall(const QString& method, const QVariantList& arguments) const
        -> QDBusPendingCall
    {
        auto message = QDBusMessage::createMethodCall(portal_service, portal_path,
                                                      shortcuts_interface, method);
        message.setArguments(arguments);
        return bus_.asyncCall(message);
    }

    [[nodiscard]] auto requestPath(const QString& handle_token) const -> QString
    {
        auto sender = bus_.baseService();
        sender.remove(u':');
        sender.replace(u'.', u'_');
        return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
            .arg(sender, handle_token);
    }

    void startRequest(const QString& method, const QVariantList& arguments,
                      const QString& handle_token)
    {
        request_path_ = requestPath(handle_token);
        if (!bus_.connect(portal_service, request_path_, request_interface, "Response", this,
                          SLOT(onPortalResponse(uint, QVariantMap))))
        {
            request_path_.clear();
            pending_operation_ = PendingOperation::none;
            emit statusChanged(QStringLiteral("Portal response signal could not be observed."));
            return;
        }

        auto* const watcher = new QDBusPendingCallWatcher{asyncPortalCall(method, arguments), this};
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher* completed) {
                    const QDBusPendingReply<QDBusObjectPath> reply = *completed;
                    completed->deleteLater();
                    if (reply.isError())
                    {
                        static_cast<void>(bus_.disconnect(
                            portal_service, request_path_, request_interface, "Response", this,
                            SLOT(onPortalResponse(uint, QVariantMap))));
                        request_path_.clear();
                        pending_operation_ = PendingOperation::none;
                        emit statusChanged(
                            QStringLiteral("Portal call failed: %1").arg(reply.error().message()));
                        return;
                    }
                    if (reply.value().path() != request_path_)
                    {
                        emit statusChanged(
                            QStringLiteral("Portal returned an unexpected request path."));
                    }
                });
    }

    void bindShortcuts()
    {
        PortalShortcuts shortcuts;
        for (const auto& [identifier, description, trigger] : std::array{
                 std::tuple{QStringLiteral("ptt_team"), QStringLiteral("Push to talk: team"),
                            QStringLiteral("CTRL+ALT+1")},
                 std::tuple{QStringLiteral("ptt_specialization"),
                            QStringLiteral("Push to talk: specialization"),
                            QStringLiteral("CTRL+ALT+2")},
                 std::tuple{QStringLiteral("ptt_group"), QStringLiteral("Push to talk: group"),
                            QStringLiteral("CTRL+ALT+3")}})
        {
            shortcuts.push_back(
                PortalShortcut{identifier, QVariantMap{{"description", description},
                                                       {"preferred_trigger", trigger}}});
        }
        QVariantMap options;
        const auto handle_token = portalToken(u"hvc_bind_");
        options.insert("handle_token", handle_token);
        pending_operation_ = PendingOperation::bind_shortcuts;
        startRequest("BindShortcuts",
                     {QVariant::fromValue(QDBusObjectPath{session_path_}),
                      QVariant::fromValue(shortcuts), QString{}, options},
                     handle_token);
    }

    void closeSession() noexcept
    {
        if (session_path_.isEmpty() || !bus_.isConnected())
        {
            return;
        }
        auto message = QDBusMessage::createMethodCall(portal_service, session_path_,
                                                      session_interface, "Close");
        static_cast<void>(bus_.call(message, QDBus::NoBlock));
        session_path_.clear();
    }

    [[nodiscard]] static auto applicationIsBackground() noexcept -> bool
    {
        return QGuiApplication::applicationState() != Qt::ApplicationActive;
    }

    QDBusConnection bus_;
    bool available_{false};
    unsigned int version_{0};
    PendingOperation pending_operation_{PendingOperation::none};
    QString request_path_;
    QString session_path_;
};

class QualityGateWindow final : public QWidget, public hvc::client::IInputEventSink
{
  public:
    QualityGateWindow()
        : portal_(this), input_source_(*this), voice_transport_(), capabilities_(new QLabel{this}),
          portal_button_(new QPushButton{tr("Create global PTT shortcuts"), this}),
          events_(new QTextEdit{this})
    {
        setWindowTitle(tr("HVC Debian/KDE Quality Gate"));
        resize(760, 520);

        auto* const layout = new QVBoxLayout{this};
        auto* const heading = new QLabel{tr("Desktop capabilities"), this};
        auto heading_font = heading->font();
        heading_font.setBold(true);
        heading->setFont(heading_font);
        layout->addWidget(heading);
        capabilities_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        capabilities_->setWordWrap(true);
        layout->addWidget(capabilities_);
        portal_button_->setMinimumHeight(44);
        layout->addWidget(portal_button_);
        events_->setReadOnly(true);
        layout->addWidget(events_, 1);

        const auto recording_devices = voice_transport_.recordingDevices();
        const auto playout_devices = voice_transport_.playoutDevices();
        const auto session_type = qEnvironmentVariable("XDG_SESSION_TYPE", "unknown");
        capabilities_->setText(
            QStringLiteral("Qt %1 · platform %2 · XDG session %3\nPortal GlobalShortcuts v%4: "
                           "%5\n%6")
                .arg(QString::fromLatin1(qVersion()), QGuiApplication::platformName(), session_type)
                .arg(portal_.version())
                .arg(portal_.available() ? QStringLiteral("available")
                                         : QStringLiteral("unavailable"),
                     tr("Recording devices: %1; playout devices: %2")
                         .arg(recording_devices.size())
                         .arg(playout_devices.size())));
        portal_button_->setEnabled(portal_.available());
        qInfo().noquote() << capabilities_->text();
        qInfo().noquote() << QStringLiteral("PORTAL_BUTTON text=\"%1\" enabled=%2")
                                 .arg(portal_button_->text(), portal_button_->isEnabled()
                                                                  ? QStringLiteral("yes")
                                                                  : QStringLiteral("no"));
        if (!portal_.available())
        {
            events_->append(tr("Global shortcut portal unavailable."));
        }

        connect(portal_button_, &QPushButton::clicked, &portal_,
                &GlobalShortcutPortal::requestShortcuts);
        connect(&portal_, &GlobalShortcutPortal::statusChanged, this,
                [this](const QString& status) {
                    events_->append(status);
                    qInfo().noquote() << status;
                });
        connect(&portal_, &GlobalShortcutPortal::shortcutStateChanged, this,
                [this](const QString& shortcut_id, bool pressed, bool background) {
                    events_->append(
                        QStringLiteral("PORTAL %1 %2 background=%3")
                            .arg(shortcut_id,
                                 pressed ? QStringLiteral("PRESS") : QStringLiteral("RELEASE"),
                                 background ? QStringLiteral("yes") : QStringLiteral("no")));
                    qInfo().noquote()
                        << QStringLiteral("PORTAL %1 %2 background=%3")
                               .arg(shortcut_id,
                                    pressed ? QStringLiteral("PRESS") : QStringLiteral("RELEASE"),
                                    background ? QStringLiteral("yes") : QStringLiteral("no"));
                });

        const auto input_result = input_source_.start();
        if (!input_result)
        {
            events_->append(tr("Controller input unavailable.") + " " +
                            QString::fromStdString(input_result.message));
        }
        else
        {
            events_->append(QStringLiteral("Linux evdev controller monitor active."));
        }
    }

    ~QualityGateWindow() override
    {
        input_source_.stop();
    }

    void onInputDeviceConnected(const hvc::client::InputDeviceProfile& profile) override
    {
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        QMetaObject::invokeMethod(
            this,
            [this, profile] {
                events_->append(QStringLiteral("HID CONNECT %1 (%2 buttons) id=%3")
                                    .arg(QString::fromStdString(profile.display_name))
                                    .arg(profile.buttons.size())
                                    .arg(QString::fromStdString(profile.device_id)));
            },
            Qt::QueuedConnection);
    }

    void onInputEvent(const hvc::client::InputEvent& event) override
    {
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        QMetaObject::invokeMethod(
            this,
            [this, event] {
                events_->append(
                    QStringLiteral("HID button=%1 %2 background-capable=%3")
                        .arg(event.control.code)
                        .arg(event.pressed ? QStringLiteral("PRESS") : QStringLiteral("RELEASE"),
                             event.received_in_background ? QStringLiteral("yes")
                                                          : QStringLiteral("no")));
            },
            Qt::QueuedConnection);
    }

    void onInputDeviceRemoved(const std::string& device_id) override
    {
        const auto copied_id = QString::fromStdString(device_id);
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        QMetaObject::invokeMethod(
            this, [this, copied_id] { events_->append("HID REMOVE " + copied_id); },
            Qt::QueuedConnection);
    }

  protected:
    void closeEvent(QCloseEvent* event) override
    {
        input_source_.stop();
        QWidget::closeEvent(event);
    }

  private:
    GlobalShortcutPortal portal_;
    hvc::client::LinuxEvdevInputSource input_source_;
    hvc::livekit::LiveKitVoiceTransport voice_transport_;
    QLabel* capabilities_;
    QPushButton* portal_button_;
    QTextEdit* events_;
};
} // namespace

auto main(int argc, char* argv[]) -> int
{
    QApplication application{argc, argv};
    QCoreApplication::setApplicationName("Hierarchical Voice Communication");
    QCoreApplication::setOrganizationName("HVC");

    QTranslator translator;
    const auto language = QLocale::system().name().section(u'_', 0, 0);
    if (translator.load(":/i18n/hvc_" + language))
    {
        QApplication::installTranslator(&translator);
    }

    QualityGateWindow window;
    window.show();
    return QApplication::exec();
}

#include "main.moc"
