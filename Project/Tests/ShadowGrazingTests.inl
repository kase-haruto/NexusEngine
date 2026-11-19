namespace {
void ShadowGrazingTests(bool warp)
{
    Fixture test(warp);
    Check(test.queue.WaitForIdle());
    test.mesh.Shutdown();
    test.CreateMesh(false, true);
    ShadowSettings settings;
    settings.mode = ShadowMode::ShadowMap;
    settings.shadowMapExtent = 2;
    settings.shadowMapResolution = 256;

    // 遮蔽物のない平面は、太陽の高度・方位やPCFの位相によらず照らされる。
    // 深度Biasを増やして影を消す修正を防ぐため、既定Biasで実GPUのMaskを検証する。
    for(const auto direction : {std::array<float, 3>{1, 0, -.05f},
        std::array<float, 3>{-1, .3f, -.1f}, std::array<float, 3>{.3f, 1, -.2f}})
    {
        const auto pixels = test.Frame(settings, 0, {}, 64, direction);
        float minimum = 1;
        for(uint32_t y = 0; y < 64; ++y)
        {
            for(uint32_t x = 0; x < 64; ++x)
            {
                minimum = (std::min)(minimum, pixels[y * 64 + x]);
            }
        }
        std::cout << "Grazing shadow minimum visibility: " << minimum << '\n';
        Require(minimum > .99f, "Unoccluded plane has grazing-angle shadow stripes.");
    }

    // 同じ補正でも、実際の遮蔽物による影は残さなければならない。
    Check(test.queue.WaitForIdle());
    test.mesh.Shutdown();
    test.CreateMesh();
    Visibility(test.Frame(settings));
    test.Validate();
    std::cout << "PASS: grazing-angle receiver has no self-shadow stripes; caster shadow retained.\n";
}
}
