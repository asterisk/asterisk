"""Add fields to ps_auths to support new algorithms

Revision ID: abdc9ede147d
Revises: 44bd6dd914fa
Create Date: 2024-10-27 15:26:25.165085
Update Date: 2025-01-28 09:50:00.000000
"""

# revision identifiers, used by Alembic.
revision = 'abdc9ede147d'
down_revision = '44bd6dd914fa'

from alembic import op
import sqlalchemy as sa

max_value_length = 1024

def upgrade():
    op.add_column('ps_auths', sa.Column('password_digest', sa.String(max_value_length)))
    op.add_column('ps_auths', sa.Column('supported_algorithms_uas', sa.String(max_value_length)))
    op.add_column('ps_auths', sa.Column('supported_algorithms_uac', sa.String(max_value_length)))
    op.add_column('ps_globals', sa.Column('default_auth_algorithms_uas', sa.String(max_value_length)))
    op.add_column('ps_globals', sa.Column('default_auth_algorithms_uac', sa.String(max_value_length)))


def downgrade():
    op.drop_column('ps_auths', 'password_digest')
    op.drop_column('ps_auths', 'supported_algorithms_uas')
    op.drop_column('ps_auths', 'supported_algorithms_uac')
    op.drop_column('ps_globals', 'default_auth_algorithms_uas')
    op.drop_column('ps_globals', 'default_auth_algorithms_uac')
