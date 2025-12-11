#ifndef PROXY_SERVER_TESTS_H_
#define PROXY_SERVER_TESTS_H_

#include <string_view>
#include <iostream>

// =============================================================================
// SHA-256 Tests
// =============================================================================

bool Sha256TestEmptyString();
bool Sha256TestNormalString();
bool Sha256TestNonAscii();
bool Sha256TestLargeString512Chars();
bool Sha256TestLargeString513Chars();
bool Sha256TestSingleCharacter();
bool Sha256TestPaddingBoundary55();
bool Sha256TestPaddingBoundary56();
bool Sha256TestRepeatedPattern();
bool Sha256TestBinaryData();
bool Sha256TestNewlineCharacters();

// =============================================================================
// Future Test Categories
// =============================================================================

// =============================================================================
// Helper Functions
// =============================================================================
bool test_helper(std::string_view expected, std::string_view result);

#endif  // PROXY_SERVER_TESTS_H_