#ifndef HVC_CLIENT_CORE_H
#define HVC_CLIENT_CORE_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(HVC_CLIENT_CORE_EXPORTS)
#define HVC_CLIENT_CORE_API __declspec(dllexport)
#else
#define HVC_CLIENT_CORE_API __declspec(dllimport)
#endif
#define HVC_CLIENT_CORE_CALL __cdecl
#else
#define HVC_CLIENT_CORE_API __attribute__((visibility("default")))
#define HVC_CLIENT_CORE_CALL
#endif

#ifdef __cplusplus
extern "C"
{
    // NOLINTBEGIN(modernize-use-using,modernize-use-trailing-return-type)
    // NOLINTBEGIN(readability-identifier-naming,performance-enum-size)
#endif

/** Major-and-minor encoded version of the first client-core ABI. */
#define HVC_CLIENT_CORE_ABI_VERSION_1 UINT32_C(0x00010000)
/** ABI version implemented by this header. */
#define HVC_CLIENT_CORE_ABI_VERSION HVC_CLIENT_CORE_ABI_VERSION_1

    /** Opaque instance owning one embedded client-core session. */
    typedef struct hvc_client_core hvc_client_core;

    /** Reference UTF-8 text without transferring ownership. */
    typedef struct hvc_client_core_string_view
    {
        /** First byte, or `NULL` when `size` is zero. */
        const char* data;
        /** Number of bytes, excluding any trailing null character. */
        size_t size;
    } hvc_client_core_string_view;

    /** Fixed-width wire type for synchronous client-core operation outcomes. */
    typedef uint32_t hvc_client_core_result;

    /** Define symbolic values for `hvc_client_core_result`. */
    enum hvc_client_core_result_value
    {
        /** The operation completed successfully. */
        HVC_CLIENT_CORE_RESULT_OK = 0,
        /** A required pointer, string, enum, or collection was invalid. */
        HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT = 1,
        /** The requested ABI major version or structure size is unsupported. */
        HVC_CLIENT_CORE_RESULT_INCOMPATIBLE_ABI = 2,
        /** Current connection or transmission state rejects the operation. */
        HVC_CLIENT_CORE_RESULT_INVALID_STATE = 3,
        /** The host-provided voice transport rejected the operation. */
        HVC_CLIENT_CORE_RESULT_TRANSPORT_ERROR = 4,
        /** An exception or other unexpected implementation failure was contained. */
        HVC_CLIENT_CORE_RESULT_INTERNAL_ERROR = 5
    };

    /** Fixed-width wire type identifying one hierarchy scope. */
    typedef uint32_t hvc_client_core_scope;

    /** Define symbolic values for `hvc_client_core_scope`. */
    enum hvc_client_core_scope_value
    {
        /** Address the current team. */
        HVC_CLIENT_CORE_SCOPE_TEAM = 0,
        /** Address the current specialization. */
        HVC_CLIENT_CORE_SCOPE_SPECIALIZATION = 1,
        /** Address the current group. */
        HVC_CLIENT_CORE_SCOPE_GROUP = 2
    };

    /** Fixed-width wire type for the aggregate connection state. */
    typedef uint32_t hvc_client_core_connection_state;

    /** Define symbolic values for `hvc_client_core_connection_state`. */
    enum hvc_client_core_connection_state_value
    {
        /** No voice-room connection is active. */
        HVC_CLIENT_CORE_CONNECTION_DISCONNECTED = 0,
        /** Granted voice-room connections are being established. */
        HVC_CLIENT_CORE_CONNECTION_CONNECTING = 1,
        /** Every granted voice-room connection is established. */
        HVC_CLIENT_CORE_CONNECTION_CONNECTED = 2,
        /** At least one established connection is recovering. */
        HVC_CLIENT_CORE_CONNECTION_RECONNECTING = 3
    };

    /** Fixed-width wire type for host voice-transport failures. */
    typedef uint32_t hvc_client_core_transport_error;

    /** Define symbolic values for `hvc_client_core_transport_error`. */
    enum hvc_client_core_transport_error_value
    {
        /** No transport error occurred. */
        HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE = 0,
        /** The transport rejected a supplied argument. */
        HVC_CLIENT_CORE_TRANSPORT_ERROR_INVALID_ARGUMENT = 1,
        /** The transport state rejects the operation. */
        HVC_CLIENT_CORE_TRANSPORT_ERROR_INVALID_STATE = 2,
        /** A voice-room connection could not be established. */
        HVC_CLIENT_CORE_TRANSPORT_ERROR_CONNECTION_FAILED = 3,
        /** No usable audio device is available. */
        HVC_CLIENT_CORE_TRANSPORT_ERROR_AUDIO_DEVICE_UNAVAILABLE = 4,
        /** The selected audio device could not be activated. */
        HVC_CLIENT_CORE_TRANSPORT_ERROR_AUDIO_DEVICE_SWITCH_FAILED = 5,
        /** Microphone publication could not be changed. */
        HVC_CLIENT_CORE_TRANSPORT_ERROR_PUBLICATION_FAILED = 6,
        /** The transport encountered an unclassified failure. */
        HVC_CLIENT_CORE_TRANSPORT_ERROR_INTERNAL = 7
    };

    /** Fixed-width wire type classifying version-one events. */
    typedef uint32_t hvc_client_core_event_kind;

    /** Define symbolic values for `hvc_client_core_event_kind`. */
    enum hvc_client_core_event_kind_value
    {
        /** A complete authoritative membership snapshot replaced the previous one. */
        HVC_CLIENT_CORE_EVENT_MEMBERSHIP_UPDATED = 1,
        /** The authoritative membership was cleared. */
        HVC_CLIENT_CORE_EVENT_MEMBERSHIP_CLEARED = 2,
        /** The aggregate voice connection state changed. */
        HVC_CLIENT_CORE_EVENT_CONNECTION_STATE_CHANGED = 3,
        /** A remote participant became audible. */
        HVC_CLIENT_CORE_EVENT_SPEAKER_STARTED = 4,
        /** A remote participant ceased to be audible. */
        HVC_CLIENT_CORE_EVENT_SPEAKER_STOPPED = 5,
        /** A synchronous operation or asynchronous transport callback failed. */
        HVC_CLIENT_CORE_EVENT_ERROR = 6
    };

    /** Grant the host transport access to one scope-specific voice room. */
    typedef struct hvc_client_core_room_grant_v1
    {
        /** Size of this structure in bytes. */
        uint32_t struct_size;
        /** Scope carried by the granted room. */
        hvc_client_core_scope scope;
        /** Voice-service URL used by the host transport. */
        hvc_client_core_string_view url;
        /** Short-lived bearer token used only by the host transport. */
        hvc_client_core_string_view token;
    } hvc_client_core_room_grant_v1;

    /** Describe one complete authoritative membership snapshot. */
    typedef struct hvc_client_core_membership_v1
    {
        /** Size of this structure in bytes. */
        uint32_t struct_size;
        /** Monotonically increasing authoritative version. */
        uint64_t version;
        /** Hierarchy definition referenced by the membership. */
        hvc_client_core_string_view hierarchy_id;
        /** Authenticated public participant identifier. */
        hvc_client_core_string_view player_id;
        /** Current group identifier. */
        hvc_client_core_string_view group_id;
        /** Current specialization identifier. */
        hvc_client_core_string_view specialization_id;
        /** Current team identifier. */
        hvc_client_core_string_view team_id;
        /** Array of role identifiers, or `NULL` when `role_count` is zero. */
        const hvc_client_core_string_view* role_ids;
        /** Number of entries in `role_ids`. */
        size_t role_count;
        /** Nonzero when the server considers the participant connected. */
        uint8_t connected;
        /** Nonzero when receiving voice is authorized. */
        uint8_t can_receive_voice;
        /** Nonzero when transmitting is prohibited. */
        uint8_t transmit_muted;
    } hvc_client_core_membership_v1;

    /**
     * Carry one immutable version-one event.
     *
     * String views, role arrays, and the membership pointer remain valid only for
     * the duration of the event callback. Fields unrelated to `kind` contain zero
     * or empty values.
     */
    typedef struct hvc_client_core_event_v1
    {
        /** Size of this structure in bytes. */
        uint32_t struct_size;
        /** ABI version used to encode this event. */
        uint32_t abi_version;
        /** Per-instance monotonically increasing event sequence. */
        uint64_t sequence;
        /** Event classification. */
        hvc_client_core_event_kind kind;
        /** New state for a connection event. */
        hvc_client_core_connection_state connection_state;
        /** Scope associated with a speaker event. */
        hvc_client_core_scope scope;
        /** Membership associated with an update event, otherwise `NULL`. */
        const hvc_client_core_membership_v1* membership;
        /** Participant associated with a speaker event. */
        hvc_client_core_string_view participant_id;
        /** Stable machine-readable code associated with an error event. */
        hvc_client_core_string_view error_code;
        /** Diagnostic detail associated with an error event. */
        hvc_client_core_string_view message;
    } hvc_client_core_event_v1;

    /**
     * Receive a client-core event.
     *
     * @param user_data Value supplied in `hvc_client_core_config_v1`.
     * @param event Borrowed event valid only until the callback returns.
     *
     * @warning The callback may run on a transport thread. It must not throw,
     *     destroy the originating handle, or retain borrowed pointers.
     */
    typedef void(HVC_CLIENT_CORE_CALL* hvc_client_core_event_callback_v1)(
        void* user_data, const hvc_client_core_event_v1* event);

    /**
     * Report a new aggregate connection state.
     *
     * @param user_data Observer context from the observer table.
     * @param state New aggregate state.
     */
    typedef void(HVC_CLIENT_CORE_CALL* hvc_client_core_connection_callback_v1)(
        void* user_data, hvc_client_core_connection_state state);

    /**
     * Report a remote audio or speaker transition.
     *
     * @param user_data Observer context from the observer table.
     * @param scope Scope containing the publication.
     * @param participant_id Non-empty public participant identifier.
     */
    typedef void(HVC_CLIENT_CORE_CALL* hvc_client_core_speaker_callback_v1)(
        void* user_data, hvc_client_core_scope scope, hvc_client_core_string_view participant_id);

    /**
     * Report an asynchronous transport failure.
     *
     * @param user_data Observer context from the observer table.
     * @param error Stable transport error classification.
     * @param message Diagnostic detail.
     */
    typedef void(HVC_CLIENT_CORE_CALL* hvc_client_core_transport_error_callback_v1)(
        void* user_data, hvc_client_core_transport_error error,
        hvc_client_core_string_view message);

    /** Receive normalized callbacks emitted by a host voice transport. */
    typedef struct hvc_client_core_transport_observer_v1
    {
        /** Size of this structure in bytes. */
        uint32_t struct_size;
        /** Opaque value passed to every observer callback. */
        void* user_data;
        /** Report a new aggregate connection state. */
        hvc_client_core_connection_callback_v1 connection_state_changed;
        /** Report a remote stream becoming available for admission. */
        hvc_client_core_speaker_callback_v1 remote_audio_available;
        /** Report a remote stream becoming unavailable. */
        hvc_client_core_speaker_callback_v1 remote_audio_unavailable;
        /** Report the start of audible remote audio. */
        hvc_client_core_speaker_callback_v1 speaker_started;
        /** Report the end of audible remote audio. */
        hvc_client_core_speaker_callback_v1 speaker_stopped;
        /** Report an asynchronous transport failure. */
        hvc_client_core_transport_error_callback_v1 error;
    } hvc_client_core_transport_observer_v1;

    /**
     * Attach or synchronously detach a transport observer.
     *
     * @param user_data Host transport context.
     * @param observer Observer to attach, or `NULL` to detach.
     * @param[out] error_message Diagnostic detail after failure.
     * @returns Stable host-transport outcome.
     */
    typedef hvc_client_core_transport_error(
        HVC_CLIENT_CORE_CALL* hvc_client_core_set_transport_observer_v1)(
        void* user_data, const hvc_client_core_transport_observer_v1* observer,
        hvc_client_core_string_view* error_message);

    /**
     * Return the current aggregate connection state.
     *
     * @param user_data Host transport context.
     * @returns Latest state.
     */
    typedef hvc_client_core_connection_state(
        HVC_CLIENT_CORE_CALL* hvc_client_core_transport_state_v1)(void* user_data);

    /**
     * Connect supplied unique room grants.
     *
     * @param user_data Host transport context.
     * @param grants Array containing one to three grants.
     * @param grant_count Number of entries in `grants`.
     * @param[out] error_message Diagnostic detail after failure.
     * @returns Stable host-transport outcome.
     */
    typedef hvc_client_core_transport_error(HVC_CLIENT_CORE_CALL* hvc_client_core_connect_rooms_v1)(
        void* user_data, const hvc_client_core_room_grant_v1* grants, size_t grant_count,
        hvc_client_core_string_view* error_message);

    /**
     * Execute a transport operation without additional input.
     *
     * @param user_data Host transport context.
     * @param[out] error_message Diagnostic detail after failure.
     * @returns Stable host-transport outcome.
     */
    typedef hvc_client_core_transport_error(
        HVC_CLIENT_CORE_CALL* hvc_client_core_transport_operation_v1)(
        void* user_data, hvc_client_core_string_view* error_message);

    /**
     * Begin microphone publication to one connected scope.
     *
     * @param user_data Host transport context.
     * @param scope Authorized publication scope.
     * @param[out] error_message Diagnostic detail after failure.
     * @returns Stable host-transport outcome.
     */
    typedef hvc_client_core_transport_error(
        HVC_CLIENT_CORE_CALL* hvc_client_core_start_microphone_v1)(
        void* user_data, hvc_client_core_scope scope, hvc_client_core_string_view* error_message);

    /**
     * Return the active microphone scope.
     *
     * @param user_data Host transport context.
     * @param[out] scope Receives the active scope when one exists.
     * @returns Nonzero when publication is active.
     */
    typedef uint8_t(HVC_CLIENT_CORE_CALL* hvc_client_core_active_microphone_scope_v1)(
        void* user_data, hvc_client_core_scope* scope);

    /**
     * Apply admission and linear gain to one remote stream.
     *
     * @param user_data Host transport context.
     * @param scope Scope containing the publication.
     * @param participant_id Non-empty public participant identifier.
     * @param admitted Nonzero when decoding and playout are permitted.
     * @param gain Linear gain in the inclusive range `[0.0F, 1.0F]`.
     * @param[out] error_message Diagnostic detail after failure.
     * @returns Stable host-transport outcome.
     */
    typedef hvc_client_core_transport_error(
        HVC_CLIENT_CORE_CALL* hvc_client_core_configure_remote_audio_v1)(
        void* user_data, hvc_client_core_scope scope, hvc_client_core_string_view participant_id,
        uint8_t admitted, float gain, hvc_client_core_string_view* error_message);

    /**
     * Define the version-one host transport table consumed by the client core.
     *
     * Every function is required. The host owns `user_data` and all transport
     * resources. Error-message views need remain valid only until the function
     * returns.
     */
    typedef struct hvc_client_core_transport_v1
    {
        /** Size of this structure in bytes. */
        uint32_t struct_size;
        /** Opaque value passed to every transport function. */
        void* user_data;
        /** Attach or synchronously detach the observer. */
        hvc_client_core_set_transport_observer_v1 set_observer;
        /** Return the current aggregate connection state. */
        hvc_client_core_transport_state_v1 state;
        /** Connect all supplied unique room grants. */
        hvc_client_core_connect_rooms_v1 connect;
        /** Disconnect every room and stop microphone publication. */
        hvc_client_core_transport_operation_v1 disconnect;
        /** Begin microphone publication to one connected scope. */
        hvc_client_core_start_microphone_v1 start_microphone;
        /** Stop active microphone publication. */
        hvc_client_core_transport_operation_v1 stop_microphone;
        /** Return whether publication is active and provide its scope. */
        hvc_client_core_active_microphone_scope_v1 active_microphone_scope;
        /** Apply admission and linear gain to one remote stream. */
        hvc_client_core_configure_remote_audio_v1 configure_remote_audio;
    } hvc_client_core_transport_v1;

    /** Configure one client-core instance. */
    typedef struct hvc_client_core_config_v1
    {
        /** Size of this structure in bytes. */
        uint32_t struct_size;
        /** Requested ABI version. */
        uint32_t abi_version;
        /** Host transport implementation and ownership context. */
        hvc_client_core_transport_v1 transport;
        /** Optional event receiver. */
        hvc_client_core_event_callback_v1 event_callback;
        /** Opaque value passed to `event_callback`. */
        void* event_user_data;
    } hvc_client_core_config_v1;

    /**
     * Return the newest ABI version implemented by the loaded library.
     *
     * @returns Major-and-minor encoded ABI version.
     */
    HVC_CLIENT_CORE_API uint32_t HVC_CLIENT_CORE_CALL hvc_client_core_api_version(void);

    /**
     * Create one client-core instance.
     *
     * @param config Versioned configuration and host transport table.
     * @param[out] core Receives the new opaque handle after success.
     * @returns Stable operation result.
     */
    HVC_CLIENT_CORE_API hvc_client_core_result HVC_CLIENT_CORE_CALL
    hvc_client_core_create(const hvc_client_core_config_v1* config, hvc_client_core** core);

    /**
     * Destroy a client-core instance.
     *
     * @param core Handle returned by `hvc_client_core_create`, or `NULL`.
     *
     * @warning The host must stop concurrent operations and transport callbacks
     *     before destruction. Observer detachment is synchronous.
     */
    HVC_CLIENT_CORE_API void HVC_CLIENT_CORE_CALL hvc_client_core_destroy(hvc_client_core* core);

    /**
     * Validate room grants and start connecting the host transport.
     *
     * @param core Client-core handle.
     * @param grants Array containing between one and three unique scope grants.
     * @param grant_count Number of entries in `grants`.
     * @returns Stable operation result. A failure also emits an error event.
     */
    HVC_CLIENT_CORE_API hvc_client_core_result HVC_CLIENT_CORE_CALL hvc_client_core_connect(
        hvc_client_core* core, const hvc_client_core_room_grant_v1* grants, size_t grant_count);

    /**
     * Stop publication and disconnect all rooms.
     *
     * @param core Client-core handle.
     * @returns Stable operation result. A failure also emits an error event.
     */
    HVC_CLIENT_CORE_API hvc_client_core_result HVC_CLIENT_CORE_CALL
    hvc_client_core_disconnect(hvc_client_core* core);

    /**
     * Start exclusive push-to-talk publication in one scope.
     *
     * @param core Client-core handle.
     * @param scope Authorized scope selected by the embedding application.
     * @returns Stable operation result. A failure also emits an error event.
     */
    HVC_CLIENT_CORE_API hvc_client_core_result HVC_CLIENT_CORE_CALL
    hvc_client_core_press_push_to_talk(hvc_client_core* core, hvc_client_core_scope scope);

    /**
     * Stop active push-to-talk publication.
     *
     * @param core Client-core handle.
     * @returns Stable operation result. A failure also emits an error event.
     */
    HVC_CLIENT_CORE_API hvc_client_core_result HVC_CLIENT_CORE_CALL
    hvc_client_core_release_push_to_talk(hvc_client_core* core);

    /**
     * Publish a complete authoritative membership snapshot.
     *
     * The core copies every string before invoking the event callback. Versions
     * must increase strictly while a membership is present.
     *
     * @param core Client-core handle.
     * @param membership Complete membership snapshot.
     * @returns Stable operation result. A failure also emits an error event.
     */
    HVC_CLIENT_CORE_API hvc_client_core_result HVC_CLIENT_CORE_CALL
    hvc_client_core_update_membership(hvc_client_core* core,
                                      const hvc_client_core_membership_v1* membership);

    /**
     * Clear the current membership and emit a cleared event.
     *
     * @param core Client-core handle.
     * @returns Stable operation result.
     */
    HVC_CLIENT_CORE_API hvc_client_core_result HVC_CLIENT_CORE_CALL
    hvc_client_core_clear_membership(hvc_client_core* core);

    /**
     * Return the latest normalized connection state.
     *
     * @param core Client-core handle.
     * @param[out] state Receives the state after success.
     * @returns Stable operation result.
     */
    HVC_CLIENT_CORE_API hvc_client_core_result HVC_CLIENT_CORE_CALL
    hvc_client_core_get_connection_state(const hvc_client_core* core,
                                         hvc_client_core_connection_state* state);

#ifdef __cplusplus
    // Public C declarations intentionally use C typedefs, fixed wire widths,
    // and snake_case names instead of C++-only forms.
    // NOLINTEND(readability-identifier-naming,performance-enum-size)
    // NOLINTEND(modernize-use-using,modernize-use-trailing-return-type)
}
#endif

#endif
