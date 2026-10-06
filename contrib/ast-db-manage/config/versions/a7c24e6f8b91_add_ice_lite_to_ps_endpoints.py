"""add ice_lite to ps_endpoints

Revision ID: a7c24e6f8b91
Revises: 2b45fd748a4f
Create Date: 2026-10-06 09:35:00.000000

"""

revision = 'a7c24e6f8b91'
down_revision = '2b45fd748a4f'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_endpoints', sa.Column('ice_lite',
                                           sa.Enum('yes', 'no',
                                                   name='yesno_values')))


def downgrade():
    op.drop_column('ps_endpoints', 'ice_lite')
