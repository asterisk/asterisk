"""Add geolocation tables

Revision ID: 2b45fd748a4f
Revises: 2285f2ace275
Create Date: 2026-08-31 09:56:07.441814

"""

# revision identifiers, used by Alembic.
revision = '2b45fd748a4f'
down_revision = '2285f2ace275'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

AST_BOOL_NAME = 'ast_bool_values'
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]

GEOLOC_LOCATION_FORMAT_NAME ='geoloc_location_format_values'
GEOLOC_LOCATION_FORMAT_VALUES = ['<none>','civicAddress','GML','URI']

GEOLOC_PROFILE_PIDF_ELEMENT_NAME = 'geoloc_profile_pidf_element_values'
GEOLOC_PROFILE_PIDF_ELEMENT_VALUES = ['<none>','tuple', 'device', 'person']

GEOLOC_PROFILE_PRECEDENCE_NAME = 'geoloc_profile_precedence_values'
GEOLOC_PROFILE_PRECEDENCE_VALUES = ['prefer_incoming', 'prefer_config', 'discard_incoming','discard_config']

def upgrade():
    enum_format = ENUM(*GEOLOC_LOCATION_FORMAT_VALUES, name=GEOLOC_LOCATION_FORMAT_NAME, checkfirst=True)
    enum_pidf_element = ENUM(*GEOLOC_PROFILE_PIDF_ELEMENT_VALUES, name=GEOLOC_PROFILE_PIDF_ELEMENT_NAME, checkfirst=True)
    enum_precedence = ENUM(*GEOLOC_PROFILE_PRECEDENCE_VALUES, name=GEOLOC_PROFILE_PRECEDENCE_NAME, checkfirst=True)
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)

    op.create_table(
        'geoloc_location',
        sa.Column('id', sa.String(80), nullable=False, primary_key=True),
        sa.Column('format', enum_format, nullable=True),
        sa.Column('location_info', sa.String(2048), nullable=True),
        sa.Column('location_source', sa.String(1024), nullable=True),
        sa.Column('confidence', sa.String(2048), nullable=True),
        sa.Column('method', sa.String(2048), nullable=True)
    )

    op.create_table(
        'geoloc_profile',
        sa.Column('id', sa.String(80), nullable=False, primary_key=True),
        sa.Column('pidf_element', enum_pidf_element, nullable=True),
        sa.Column('pidf_element_id', sa.String(80), nullable=True),
        sa.Column('device_id', sa.String(1024), nullable=True),
        sa.Column('location_reference', sa.String(80), nullable=True),
        sa.Column('location_info_refinement', sa.String(2048), nullable=True),
        sa.Column('location_variables', sa.String(2048), nullable=True),
        sa.Column('usage_rules', sa.String(2048), nullable=True),
        sa.Column('notes', sa.String(2048), nullable=True),
        sa.Column('allow_routing_use', ast_bool_values),
        sa.Column('suppress_empty_ca_elements', ast_bool_values),
        sa.Column('profile_precedence', enum_precedence, nullable=True),
        sa.Column('format', enum_format, nullable=True),
        sa.Column('location_info', sa.String(2048), nullable=True),
        sa.Column('location_source', sa.String(1024), nullable=True),
        sa.Column('confidence', sa.String(2048), nullable=True),
        sa.Column('method', sa.String(2048), nullable=True)
    )

def downgrade():
    op.drop_table('geoloc_location')
    op.drop_table('geoloc_profile')
    sa.Enum(*GEOLOC_LOCATION_FORMAT_VALUES, name=GEOLOC_LOCATION_FORMAT_NAME).drop(op.get_bind(), checkfirst=True)
    sa.Enum(*GEOLOC_PROFILE_PIDF_ELEMENT_VALUES, name=GEOLOC_PROFILE_PIDF_ELEMENT_NAME).drop(op.get_bind(), checkfirst=True)
    sa.Enum(*GEOLOC_PROFILE_PRECEDENCE_VALUES, name=GEOLOC_PROFILE_PRECEDENCE_NAME).drop(op.get_bind(), checkfirst=True)
