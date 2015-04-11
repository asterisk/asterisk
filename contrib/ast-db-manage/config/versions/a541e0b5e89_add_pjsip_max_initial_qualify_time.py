"""add pjsip max_initial_qualify_time

Revision ID: a541e0b5e89
Revises: 461d7d691209
Create Date: 2015-04-15 14:37:36.424471

"""

# revision identifiers, used by Alembic.
revision = 'a541e0b5e89'
down_revision = '461d7d691209'

from alembic import op
import sqlalchemy as sa

def upgrade():
    op.add_column('ps_globals', sa.Column('max_initial_qualify_time', sa.Integer))

def downgrade():
    op.drop_column('ps_globals', 'max_initial_qualify_time')
