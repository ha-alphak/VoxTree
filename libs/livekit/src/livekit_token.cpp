#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <hvc/livekit/livekit_token.hpp>
#include <stdexcept>
#include <utility>

namespace hvc::livekit
{
namespace
{
using Hash = std::array<std::uint8_t, 32>;

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

[[nodiscard]] auto sha256(std::string_view input) -> Hash
{
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const auto bit_length = static_cast<std::uint64_t>(message.size()) * 8U;
    message.push_back(0x80U);
    while (message.size() % 64U != 56U)
    {
        message.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        message.push_back(static_cast<std::uint8_t>(bit_length >> shift));
    }

    std::array<std::uint32_t, 8> state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                       0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    for (std::size_t offset = 0; offset < message.size(); offset += 64U)
    {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index)
        {
            const auto byte_offset = offset + (index * 4U);
            words[index] = (static_cast<std::uint32_t>(message[byte_offset]) << 24U) |
                           (static_cast<std::uint32_t>(message[byte_offset + 1U]) << 16U) |
                           (static_cast<std::uint32_t>(message[byte_offset + 2U]) << 8U) |
                           static_cast<std::uint32_t>(message[byte_offset + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index)
        {
            const auto left = std::rotr(words[index - 15U], 7) ^ std::rotr(words[index - 15U], 18) ^
                              (words[index - 15U] >> 3U);
            const auto right = std::rotr(words[index - 2U], 17) ^ std::rotr(words[index - 2U], 19) ^
                               (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + left + words[index - 7U] + right;
        }

        auto working_a = state[0];
        auto working_b = state[1];
        auto working_c = state[2];
        auto working_d = state[3];
        auto working_e = state[4];
        auto working_f = state[5];
        auto working_g = state[6];
        auto working_h = state[7];
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            const auto sum1 =
                std::rotr(working_e, 6) ^ std::rotr(working_e, 11) ^ std::rotr(working_e, 25);
            const auto choice = (working_e & working_f) ^ (~working_e & working_g);
            const auto temp1 = working_h + sum1 + choice + round_constants[index] + words[index];
            const auto sum0 =
                std::rotr(working_a, 2) ^ std::rotr(working_a, 13) ^ std::rotr(working_a, 22);
            const auto majority =
                (working_a & working_b) ^ (working_a & working_c) ^ (working_b & working_c);
            const auto temp2 = sum0 + majority;
            working_h = working_g;
            working_g = working_f;
            working_f = working_e;
            working_e = working_d + temp1;
            working_d = working_c;
            working_c = working_b;
            working_b = working_a;
            working_a = temp1 + temp2;
        }
        state[0] += working_a;
        state[1] += working_b;
        state[2] += working_c;
        state[3] += working_d;
        state[4] += working_e;
        state[5] += working_f;
        state[6] += working_g;
        state[7] += working_h;
    }

    Hash result{};
    for (std::size_t index = 0; index < state.size(); ++index)
    {
        const auto byte_offset = index * 4U;
        result[byte_offset] = static_cast<std::uint8_t>(state[index] >> 24U);
        result[byte_offset + 1U] = static_cast<std::uint8_t>(state[index] >> 16U);
        result[byte_offset + 2U] = static_cast<std::uint8_t>(state[index] >> 8U);
        result[byte_offset + 3U] = static_cast<std::uint8_t>(state[index]);
    }
    return result;
}

[[nodiscard]] auto hmacSha256(std::string_view key, std::string_view message) -> Hash
{
    std::array<std::uint8_t, 64> normalized{};
    if (key.size() > normalized.size())
    {
        const auto hashed = sha256(key);
        std::ranges::copy(hashed, normalized.begin());
    }
    else
    {
        std::ranges::copy(key, normalized.begin());
    }
    std::string inner(64, '\0');
    std::string outer(64, '\0');
    for (std::size_t index = 0; index < normalized.size(); ++index)
    {
        inner[index] = static_cast<char>(normalized[index] ^ 0x36U);
        outer[index] = static_cast<char>(normalized[index] ^ 0x5cU);
    }
    inner += message;
    const auto inner_hash = sha256(inner);
    outer.append(reinterpret_cast<const char*>(inner_hash.data()), inner_hash.size());
    return sha256(outer);
}

[[nodiscard]] auto base64Url(const std::uint8_t* data, std::size_t size) -> std::string
{
    constexpr std::string_view alphabet{
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"};
    std::string output;
    output.reserve((size * 4U + 2U) / 3U);
    for (std::size_t index = 0; index < size; index += 3U)
    {
        const auto remaining = size - index;
        const auto value =
            (static_cast<std::uint32_t>(data[index]) << 16U) |
            (remaining > 1U ? static_cast<std::uint32_t>(data[index + 1U]) << 8U : 0U) |
            (remaining > 2U ? static_cast<std::uint32_t>(data[index + 2U]) : 0U);
        output.push_back(alphabet[(value >> 18U) & 63U]);
        output.push_back(alphabet[(value >> 12U) & 63U]);
        if (remaining > 1U)
        {
            output.push_back(alphabet[(value >> 6U) & 63U]);
        }
        if (remaining > 2U)
        {
            output.push_back(alphabet[value & 63U]);
        }
    }
    return output;
}

[[nodiscard]] auto base64Url(std::string_view value) -> std::string
{
    return base64Url(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
}

[[nodiscard]] auto jsonEscape(std::string_view value) -> std::string
{
    std::string result{"\""};
    for (const auto character : value)
    {
        if (character == '"' || character == '\\')
        {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] auto roomFor(const application::VoiceGrantClaims& claims, domain::VoiceScope scope)
    -> std::string
{
    switch (scope)
    {
    case domain::VoiceScope::team:
        return "team:" + std::string{claims.team_id.value()};
    case domain::VoiceScope::specialization:
        return "specialization:" + std::string{claims.specialization_id.value()};
    case domain::VoiceScope::group:
        return "group:" + std::string{claims.group_id.value()};
    }
    throw std::invalid_argument{"unsupported voice scope"};
}

[[nodiscard]] auto contains(const std::vector<domain::VoiceScope>& scopes, domain::VoiceScope scope)
    -> bool
{
    return std::ranges::find(scopes, scope) != scopes.end();
}
} // namespace

LiveKitCredentials::LiveKitCredentials(std::string key, std::string secret)
    : api_key(std::move(key)), api_secret(std::move(secret))
{
    if (api_key.empty() || api_secret.empty())
    {
        throw std::invalid_argument{"LiveKit API key and secret must not be empty"};
    }
}

LiveKitTokenAdapter::LiveKitTokenAdapter(LiveKitCredentials credentials)
    : credentials_(std::move(credentials))
{
}

auto LiveKitTokenAdapter::sign(const application::VoiceGrantClaims& claims) const
    -> std::vector<SignedRoomGrant>
{
    constexpr std::array scopes{domain::VoiceScope::team, domain::VoiceScope::specialization,
                                domain::VoiceScope::group};
    const auto expiration =
        std::chrono::duration_cast<std::chrono::seconds>(claims.expires_at.time_since_epoch())
            .count();
    std::vector<SignedRoomGrant> grants;
    for (const auto scope : scopes)
    {
        const auto can_publish = contains(claims.transmit_scopes, scope);
        const auto can_subscribe = contains(claims.receive_scopes, scope);
        if (!can_publish && !can_subscribe)
        {
            continue;
        }
        const auto room = roomFor(claims, scope);
        const auto metadata =
            "{\"device_id\":" + jsonEscape(claims.device_id.value()) +
            ",\"membership_version\":" + std::to_string(claims.membership_version) + '}';
        const auto payload = "{\"iss\":" + jsonEscape(credentials_.api_key) +
                             ",\"sub\":" + jsonEscape(claims.player_id.value()) +
                             ",\"exp\":" + std::to_string(expiration) +
                             ",\"metadata\":" + jsonEscape(metadata) +
                             ",\"video\":{\"roomJoin\":true,\"room\":" + jsonEscape(room) +
                             ",\"canPublish\":" + (can_publish ? "true" : "false") +
                             ",\"canSubscribe\":" + (can_subscribe ? "true" : "false") + "}}";
        const auto signing_input =
            base64Url(R"({"alg":"HS256","typ":"JWT"})") + '.' + base64Url(payload);
        const auto signature = hmacSha256(credentials_.api_secret, signing_input);
        grants.push_back(
            {scope, room, signing_input + '.' + base64Url(signature.data(), signature.size())});
    }
    return grants;
}
} // namespace hvc::livekit
