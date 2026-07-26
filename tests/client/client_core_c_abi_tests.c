#include <hvc/client_core.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct fake_transport
{
    const hvc_client_core_transport_observer_v1* observer;
    hvc_client_core_connection_state state;
    uint8_t microphone_active;
    hvc_client_core_scope microphone_scope;
    unsigned int event_count;
} fake_transport;

static hvc_client_core_string_view text(const char* value)
{
    hvc_client_core_string_view result = {value, strlen(value)};
    return result;
}

static hvc_client_core_transport_error HVC_CLIENT_CORE_CALL
set_observer(void* user_data, const hvc_client_core_transport_observer_v1* observer,
             hvc_client_core_string_view* error_message)
{
    fake_transport* transport = (fake_transport*)user_data;
    transport->observer = observer;
    error_message->data = NULL;
    error_message->size = 0;
    return HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE;
}

static hvc_client_core_connection_state HVC_CLIENT_CORE_CALL state(void* user_data)
{
    return ((fake_transport*)user_data)->state;
}

static hvc_client_core_transport_error HVC_CLIENT_CORE_CALL
connect_rooms(void* user_data, const hvc_client_core_room_grant_v1* grants, size_t grant_count,
              hvc_client_core_string_view* error_message)
{
    fake_transport* transport = (fake_transport*)user_data;
    if (grants == NULL || grant_count != 1)
    {
        *error_message = text("one grant was expected");
        return HVC_CLIENT_CORE_TRANSPORT_ERROR_INVALID_ARGUMENT;
    }
    transport->state = HVC_CLIENT_CORE_CONNECTION_CONNECTED;
    transport->observer->connection_state_changed(transport->observer->user_data, transport->state);
    return HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE;
}

static hvc_client_core_transport_error HVC_CLIENT_CORE_CALL
disconnect_rooms(void* user_data, hvc_client_core_string_view* error_message)
{
    fake_transport* transport = (fake_transport*)user_data;
    (void)error_message;
    transport->microphone_active = 0;
    transport->state = HVC_CLIENT_CORE_CONNECTION_DISCONNECTED;
    transport->observer->connection_state_changed(transport->observer->user_data, transport->state);
    return HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE;
}

static hvc_client_core_transport_error HVC_CLIENT_CORE_CALL start_microphone(
    void* user_data, hvc_client_core_scope scope, hvc_client_core_string_view* error_message)
{
    fake_transport* transport = (fake_transport*)user_data;
    (void)error_message;
    transport->microphone_active = 1;
    transport->microphone_scope = scope;
    return HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE;
}

static hvc_client_core_transport_error HVC_CLIENT_CORE_CALL
stop_microphone(void* user_data, hvc_client_core_string_view* error_message)
{
    fake_transport* transport = (fake_transport*)user_data;
    (void)error_message;
    transport->microphone_active = 0;
    return HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE;
}

static uint8_t HVC_CLIENT_CORE_CALL active_microphone_scope(void* user_data,
                                                            hvc_client_core_scope* scope)
{
    fake_transport* transport = (fake_transport*)user_data;
    *scope = transport->microphone_scope;
    return transport->microphone_active;
}

static hvc_client_core_transport_error HVC_CLIENT_CORE_CALL configure_remote_audio(
    void* user_data, hvc_client_core_scope scope, hvc_client_core_string_view participant_id,
    uint8_t admitted, float gain, hvc_client_core_string_view* error_message)
{
    (void)user_data;
    (void)scope;
    (void)participant_id;
    (void)admitted;
    (void)gain;
    (void)error_message;
    return HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE;
}

static void HVC_CLIENT_CORE_CALL receive_event(void* user_data,
                                               const hvc_client_core_event_v1* event)
{
    fake_transport* transport = (fake_transport*)user_data;
    if (event->abi_version == HVC_CLIENT_CORE_ABI_VERSION)
    {
        ++transport->event_count;
    }
}

static int run_test(void)
{
    fake_transport transport = {0};
    hvc_client_core_config_v1 config = {0};
    hvc_client_core_room_grant_v1 grant = {0};
    hvc_client_core_connection_state current_state;
    hvc_client_core* core = NULL;

    config.struct_size = sizeof(config);
    config.abi_version = HVC_CLIENT_CORE_ABI_VERSION;
    config.transport.struct_size = sizeof(config.transport);
    config.transport.user_data = &transport;
    config.transport.set_observer = set_observer;
    config.transport.state = state;
    config.transport.connect = connect_rooms;
    config.transport.disconnect = disconnect_rooms;
    config.transport.start_microphone = start_microphone;
    config.transport.stop_microphone = stop_microphone;
    config.transport.active_microphone_scope = active_microphone_scope;
    config.transport.configure_remote_audio = configure_remote_audio;
    config.event_callback = receive_event;
    config.event_user_data = &transport;

    if (hvc_client_core_create(&config, &core) != HVC_CLIENT_CORE_RESULT_OK)
    {
        return 1;
    }

    grant.struct_size = sizeof(grant);
    grant.scope = HVC_CLIENT_CORE_SCOPE_TEAM;
    grant.url = text("wss://voice.example.invalid");
    grant.token = text("short-lived-token");
    if (hvc_client_core_connect(core, &grant, 1) != HVC_CLIENT_CORE_RESULT_OK ||
        hvc_client_core_get_connection_state(core, &current_state) != HVC_CLIENT_CORE_RESULT_OK ||
        current_state != HVC_CLIENT_CORE_CONNECTION_CONNECTED ||
        hvc_client_core_press_push_to_talk(core, HVC_CLIENT_CORE_SCOPE_TEAM) !=
            HVC_CLIENT_CORE_RESULT_OK ||
        hvc_client_core_release_push_to_talk(core) != HVC_CLIENT_CORE_RESULT_OK ||
        hvc_client_core_disconnect(core) != HVC_CLIENT_CORE_RESULT_OK)
    {
        hvc_client_core_destroy(core);
        return 1;
    }
    hvc_client_core_destroy(core);
    return transport.observer == NULL && transport.event_count == 2 ? 0 : 1;
}

int main(void)
{
    const int result = run_test();
    if (result != 0)
    {
        fputs("pure C client-core ABI test failed\n", stderr);
    }
    return result;
}
