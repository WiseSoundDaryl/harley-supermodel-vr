obj64/ppc.o: CXXFLAGS += -fprofile-use=BuildProfiles -fprofile-partial-training -Werror=missing-profile -Werror=coverage-mismatch -flto=8
LDFLAGS += -flto=8 -O3
