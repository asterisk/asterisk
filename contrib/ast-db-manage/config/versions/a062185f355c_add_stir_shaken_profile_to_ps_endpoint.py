"""Add Stir Shaken Profile to ps endpoint

Revision ID: a062185f355c
Revises: 7197536bb68d
Create Date: 2022-08-17 13:03:50.149536

"""

# revision identifiers, used by Alembic.
revision = 'a062185f355c'
down_revision = '7197536bb68d'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('stir_shaken_profile', sa.String(80)))


def downgrade():
    op.drop_column('ps_endpoints', 'stir_shaken_profile')
