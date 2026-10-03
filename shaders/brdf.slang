// Les termes de la BRDF de Cook-Torrance que partagent l'éclairage direct (mesh.slang) et le
// précalcul de l'IBL (environment.slang), d'après l'annexe B de la spécification glTF : la même
// distribution et la même visibilité des deux côtés, ou les deux éclairages ne s'accorderaient pas.
// Inclus, jamais compilé seul.

static const float Pi = 3.14159265358979;

/// La distribution GGX des micro-facettes, pour une rugosité `alpha` (le carré de celle du
/// matériau) : la part des facettes orientées selon la demi-direction, à `cosTheta` de la normale.
float ggxDistributionOf(float cosTheta, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float denominator = cosTheta * cosTheta * (alpha2 - 1.0) + 1.0;
    return alpha2 / (Pi * denominator * denominator);
}

/// La visibilité de Smith « height-correlated » (Heitz 2014) : G / (4 n·l n·v), où l'ombrage des
/// micro-facettes vu de l'œil et vu de la lumière dépendent l'un de l'autre. Celle de la spécification
/// glTF (annexe B) et du glTF Sample Viewer, plus juste que le produit séparé de Karis aux angles
/// rasants.
float smithVisibilityOf(float cosView, float cosLight, float alpha)
{
    const float alpha2 = alpha * alpha;
    const float fromView = cosLight * sqrt(cosView * cosView * (1.0 - alpha2) + alpha2);
    const float fromLight = cosView * sqrt(cosLight * cosLight * (1.0 - alpha2) + alpha2);
    return 0.5 / (fromView + fromLight);
}
