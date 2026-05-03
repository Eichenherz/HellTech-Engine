[shader("pixel")]
float4 ColPassPsMain( in float4 col : COLOR0 ) : SV_Target
{
	return col;
}