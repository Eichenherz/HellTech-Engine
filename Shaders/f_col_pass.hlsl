[shader("pixel")]
float4 ColPassPSMain( [[vk::location( 0 )]] in float4 col : COLOR0 ) : SV_Target
{
	return col;
}