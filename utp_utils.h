#ifdef __cplusplus
extern "C" {
#endif

// This should return the MTU to the destination
uint16_t UTP_GetUDPMTU(const struct sockaddr *remote, socklen_t remotelen);
// This should return the number of bytes of UDP overhead for one packet to the
// destination, for overhead calculation only
uint16_t UTP_GetUDPOverhead(const struct sockaddr *remote, socklen_t remotelen);
// This should return monotonically increasing milliseconds, start point does not matter
uint32_t UTP_GetMilliseconds();
// This should return monotonically increasing microseconds, start point does not matter
uint64_t UTP_GetMicroseconds();
// This should return a random uint32_t
uint32_t UTP_Random();
// This is called every time we have a delay sample is made
void UTP_DelaySample(const struct sockaddr *remote, int sample_ms);
// Should return the max packet size to use when sending to the given address
size_t UTP_GetPacketSizeForAddr(const struct sockaddr *remote);

#ifdef __cplusplus
}
#endif
