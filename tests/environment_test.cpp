#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <doctest/doctest.h>

#include "levain/render/environment.hpp"

namespace
{

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 32;

/// Un ciel uniforme de luminance 1.
std::vector<float> uniformSky()
{
    std::vector<float> rgba(std::size_t{Width} * Height * 4, 1.0f);
    return rgba;
}

void setPixel(std::vector<float>& rgba, std::uint32_t x, std::uint32_t y, glm::vec3 color)
{
    const std::size_t index = ((std::size_t{y} * Width) + x) * 4;
    rgba[index] = color.r;
    rgba[index + 1] = color.g;
    rgba[index + 2] = color.b;
}

glm::vec3 pixelAt(const std::vector<float>& rgba, std::uint32_t x, std::uint32_t y)
{
    const std::size_t index = ((std::size_t{y} * Width) + x) * 4;
    return {rgba[index], rgba[index + 1], rgba[index + 2]};
}

glm::vec3 directionOfPixel(std::uint32_t x, std::uint32_t y)
{
    return levain::render::equirectDirectionOf(
        {(static_cast<float>(x) + 0.5f) / Width, (static_cast<float>(y) + 0.5f) / Height});
}

float luminanceOf(glm::vec3 color)
{
    return glm::dot(color, glm::vec3{0.2126f, 0.7152f, 0.0722f});
}

} // namespace

TEST_CASE("un ciel sans pixel qui se détache n'a pas de soleil, et reste tel quel")
{
    std::vector<float> rgba = uniformSky();
    setPixel(rgba, 10, 10, glm::vec3{50.0f}); // un nuage éclairé : 50 fois la moyenne, pas 1000
    const std::vector<float> before = rgba;
    CHECK_FALSE(levain::render::extractSun(Width, Height, rgba).has_value());
    CHECK(rgba == before);
}

TEST_CASE(
    "le soleil d'une HDRI : sa direction, son énergie, et un ciel qui n'en garde que le seuil")
{
    std::vector<float> rgba = uniformSky();
    const glm::vec3 sunColor{1.0e5f, 0.9e5f, 0.8e5f};
    setPixel(rgba, 40, 10, sunColor);
    // Assez brillant pour passer le seuil, mais loin du soleil : une autre lumière, qui reste.
    const glm::vec3 lamp{3.0e4f};
    setPixel(rgba, 8, 24, lamp);

    const auto extracted = levain::render::extractSun(Width, Height, rgba);
    REQUIRE(extracted.has_value());
    const levain::render::Sun sun = extracted.value_or(levain::render::Sun{});
    CHECK(glm::dot(sun.direction, directionOfPixel(40, 10)) == doctest::Approx(1.0f));
    // L'énergie retirée : ce qui dépassait le seuil, fois l'angle solide du pixel.
    const float threshold = luminanceOf(sunColor) * levain::render::SunThresholdRatio;
    const float solidAngle = 2.0f * std::numbers::pi_v<float> / Width * std::numbers::pi_v<float> /
                             Height * std::sin((10.5f / Height) * std::numbers::pi_v<float>);
    CHECK(sun.intensity ==
          doctest::Approx((luminanceOf(sunColor) - threshold) * solidAngle).epsilon(1e-4));
    CHECK(luminanceOf(sun.color) == doctest::Approx(1.0f));
    CHECK(sun.color.r > sun.color.b); // la couleur du soleil, plus chaude que blanche

    CHECK(luminanceOf(pixelAt(rgba, 40, 10)) == doctest::Approx(threshold));
    CHECK(pixelAt(rgba, 8, 24) == lamp);
    CHECK(pixelAt(rgba, 0, 0) == glm::vec3{1.0f});
}
