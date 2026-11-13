"""Add pin_media_source to ps_transports

Revision ID: c3a1e9f20b47
Revises: 2b45fd748a4f
Create Date: 2026-11-08 20:00:00.000000

"""

# revision identifiers, used by Alembic.
revision = 'c3a1e9f20b47'
down_revision = '2b45fd748a4f'

from alembic import op
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM

AST_BOOL_NAME = 'ast_bool_values'
# We'll just ignore the n/y and f/t abbreviations as Asterisk does not write
# those aliases.
AST_BOOL_VALUES = [ '0', '1',
                    'off', 'on',
                    'false', 'true',
                    'no', 'yes' ]


def upgrade():
    ast_bool_values = ENUM(*AST_BOOL_VALUES, name=AST_BOOL_NAME, create_type=False)
    op.add_column('ps_transports', sa.Column('pin_media_source', ast_bool_values))


def downgrade():
    if op.get_context().bind.dialect.name == 'mssql':
        op.drop_constraint('ck_ps_transports_pin_media_source_ast_bool_values', 'ps_transports')
    op.drop_column('ps_transports', 'pin_media_source')
